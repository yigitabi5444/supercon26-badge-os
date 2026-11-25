#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "txn_vfs.h"

static am_result_t error(void) { return errno==ENOSPC?AM_FULL:errno==ENOENT?AM_NOT_FOUND:AM_IO; }
static int entry_stat(const char *name,struct stat *st)
{
#ifdef ESP_PLATFORM
    // Target caller is pinned LittleFS1.23.1 only: lfs_info exposes REG/DIR, no symlinks.
    // IDF VFS implements stat, not lstat. Do not reuse this branch for other filesystems.
    return stat(name,st);
#else
    return lstat(name,st); // Preserve host rejection of links, including dangling destinations.
#endif
}
static bool path(am_vfs_t *h,const char *relative,char out[AM_VFS_PATH_MAX])
{
    if(strlen(relative)>=AM_PATH_MAX || strstr(relative,"..") || relative[0]=='/')return false;
    int n=snprintf(out,AM_VFS_PATH_MAX,"%s/%s",h->root,relative);
    return n>0 && n<AM_VFS_PATH_MAX;
}
static am_result_t read_file(void *context,const char *relative,uint8_t *out,size_t cap,size_t *size)
{
    if(cap>AM_PAYLOAD_MAX)return AM_LIMIT;
    char p[AM_VFS_PATH_MAX];if(!path(context,relative,p))return AM_LIMIT;
    int fd=open(p,O_RDONLY);if(fd<0)return error();
    struct stat st;am_result_t r=AM_OK;size_t n=0;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<0)r=AM_IO;
    else if((uint64_t)st.st_size>cap)r=AM_LIMIT;
    else while(n<(size_t)st.st_size) {
        ssize_t got=read(fd,out+n,(size_t)st.st_size-n);
        if(got<0 && errno==EINTR)continue;
        if(got<=0){r=AM_IO;break;}n+=(size_t)got;
    }
    if(close(fd) && r==AM_OK)r=AM_IO;
    *size=n;return r;
}
static am_result_t read_at(void *context,const char *relative,uint32_t offset,uint8_t *out,size_t cap,size_t *size)
{
    if(!out || !size || !cap || cap>4096)return AM_LIMIT;
    char p[AM_VFS_PATH_MAX];if(!path(context,relative,p))return AM_LIMIT;
    int fd=open(p,O_RDONLY);if(fd<0)return error();
    struct stat st;am_result_t r=AM_OK;size_t n=0;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<0)r=AM_IO;
    else if(offset>(uint64_t)st.st_size)r=AM_LIMIT;
    else if(lseek(fd,(off_t)offset,SEEK_SET)!=(off_t)offset)r=AM_IO;
    else {
        uint64_t left=(uint64_t)st.st_size-offset;if(left<cap)cap=(size_t)left;
        while(n<cap) {
            ssize_t got=read(fd,out+n,cap-n);
            if(got<0 && errno==EINTR)continue;
            if(got<=0){r=AM_IO;break;}n+=(size_t)got;
        }
    }
    if(close(fd) && r==AM_OK)r=AM_IO;
    *size=n;return r;
}
static am_result_t write_file(void *context,const char *relative,size_t offset,const uint8_t *bytes,size_t size)
{
    if(size>4096 || offset>UINT32_MAX || size>UINT32_MAX-offset)return AM_LIMIT;
    am_vfs_t *h=context;char p[AM_VFS_PATH_MAX];if(!path(h,relative,p))return AM_LIMIT;
    int fd=open(p,O_WRONLY|(offset?0:O_CREAT|O_EXCL),0600);if(fd<0)return error();
    struct stat st;am_result_t r=AM_OK;size_t n=0;
    if(fstat(fd,&st) || st.st_size<0 || (uint64_t)st.st_size!=offset)r=AM_IO;
    if(r==AM_OK && lseek(fd,(off_t)offset,SEEK_SET)!=(off_t)offset)r=AM_IO;
    while(r==AM_OK && n<size) {
        ssize_t put=write(fd,bytes+n,size-n);
        if(put<0 && errno==EINTR)continue;
        if(put<=0){r=error();break;}n+=(size_t)put;
    }
    if(r==AM_OK && fsync(fd))r=AM_IO;
    if(close(fd) && r==AM_OK)r=AM_IO;
    return r;
}
static am_result_t make_directory(void *context,const char *relative)
{
    char p[AM_VFS_PATH_MAX];struct stat st;if(!path(context,relative,p))return AM_LIMIT;
    if(!mkdir(p,0700))return AM_OK;
    if(errno==EEXIST && !stat(p,&st) && S_ISDIR(st.st_mode))return AM_OK;
    return error();
}
static am_result_t sync_path(void *context,const char *relative)
{
    char p[AM_VFS_PATH_MAX];if(!path(context,relative,p))return AM_LIMIT;
    int fd=open(p,O_RDONLY);if(fd<0)return error();
    int r=fsync(fd),closed=close(fd);return r || closed?AM_IO:AM_OK;
}
static am_result_t move_path(void *context,const char *from,const char *to,bool replace)
{
    char a[AM_VFS_PATH_MAX],b[AM_VFS_PATH_MAX];struct stat st;
    if(!path(context,from,a) || !path(context,to,b))return AM_LIMIT;
    if(!replace) {
        if(!entry_stat(b,&st))return AM_IO;
        if(errno!=ENOENT)return error();
    }
    return rename(a,b)?error():AM_OK;
}
static am_result_t children(am_vfs_t *h,const char *relative,char names[8][80],size_t *count)
{
    char p[AM_VFS_PATH_MAX];if(!path(h,relative,p))return AM_LIMIT;
    DIR *dir=opendir(p);if(!dir)return error();*count=0;am_result_t r=AM_OK;
    struct dirent *entry;errno=0;
    while((entry=readdir(dir))) {
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
        size_t n=strnlen(entry->d_name,80);
        if(n==80 || *count==8){r=AM_LIMIT;break;}
        memcpy(names[(*count)++],entry->d_name,n+1);errno=0;
    }
    if(!entry && errno)r=AM_IO;
    if(closedir(dir) && r==AM_OK)r=AM_IO;
    return r;
}
// One directory cursor at a time; closed before descending or mutating.
static am_result_t walk(am_vfs_t *h,const char *relative,unsigned depth,bool erase,uint64_t *used)
{
    char p[AM_VFS_PATH_MAX];struct stat st;
    if(depth>4 || !path(h,relative,p))return AM_LIMIT;
    if(entry_stat(p,&st))return error();
    if(S_ISREG(st.st_mode)) {
        if(st.st_size<0 || UINT64_MAX-*used<(uint64_t)st.st_size)return AM_LIMIT;
        *used+=(uint64_t)st.st_size;return erase && unlink(p)?error():AM_OK;
    }
    if(!S_ISDIR(st.st_mode))return AM_IO;
    char names[8][80];size_t count=0;am_result_t r=children(h,relative,names,&count);
    if(r!=AM_OK)return r;
    for(size_t i=0;i<count;i++) {
        char child[AM_PATH_MAX];int n=snprintf(child,sizeof child,"%s%s%s",relative,*relative?"/":"",names[i]);
        if(n<0 || (size_t)n>=sizeof child)return AM_LIMIT;
        r=walk(h,child,depth+1,erase,used);if(r!=AM_OK)return r;
    }
    if(r==AM_OK && erase && rmdir(p))r=error();
    return r;
}
static am_result_t remove_path(void *context,const char *relative)
{
    uint64_t ignored=0;am_result_t r=walk(context,relative,0,true,&ignored);
    return r==AM_NOT_FOUND?AM_OK:r;
}
static am_result_t versions(void *context,char tokens[AM_VERSIONS_MAX][65],size_t *count)
{
    char names[8][80];am_result_t r=children(context,"versions",names,count);
    if(r!=AM_OK)return r;
    for(size_t i=0;i<*count;i++) {
        if(!am_token_valid(names[i]))return AM_BAD_RECORD;
        memcpy(tokens[i],names[i],65);
    }
    return AM_OK;
}
static am_result_t space(void *context,uint64_t *used,uint64_t *capacity)
{
    am_vfs_t *h=context;*used=0;*capacity=h->quota;
    return walk(h,"",0,false,used);
}
static am_result_t deny_path(void *ctx,const char *p){(void)ctx;(void)p;return AM_DISABLED;}
static am_result_t deny_move(void *ctx,const char *a,const char *b,bool replace)
{(void)ctx;(void)a;(void)b;(void)replace;return AM_DISABLED;}
static am_result_t deny_write(void *ctx,const char *p,size_t off,const uint8_t *data,size_t n)
{(void)ctx;(void)p;(void)off;(void)data;(void)n;return AM_DISABLED;}
static am_result_t bind_io(am_vfs_t *h,const char *root,uint64_t quota,am_io_t *out,bool readonly)
{
    if(strlen(root)>=sizeof h->root)return AM_LIMIT;
    memset(h,0,sizeof *h);strcpy(h->root,root);h->quota=quota;
    if(readonly) {
        struct stat st;if(entry_stat(root,&st))return error();
        if(!S_ISDIR(st.st_mode))return AM_IO;
    } else {
        if(mkdir(root,0700) && errno!=EEXIST)return error();
        const char *dirs[]={"versions","stage","active","data"};
        for(unsigned i=0;i<4;i++){am_result_t r=make_directory(h,dirs[i]);if(r!=AM_OK)return r;}
    }
    *out=(am_io_t){.context=h,.read=read_file,.write=write_file,.mkdir=make_directory,
        .sync=sync_path,.move=move_path,.remove=remove_path,.versions=versions,.space=space,.read_at=read_at};
    if(readonly){out->write=deny_write;out->mkdir=out->sync=out->remove=deny_path;out->move=deny_move;}
    return AM_OK;
}
am_result_t am_vfs_io(am_vfs_t *h,const char *root,uint64_t quota,am_io_t *out)
{return bind_io(h,root,quota,out,false);}
am_result_t am_vfs_readonly_io(am_vfs_t *h,const char *root,uint64_t quota,am_io_t *out)
{return bind_io(h,root,quota,out,true);}
