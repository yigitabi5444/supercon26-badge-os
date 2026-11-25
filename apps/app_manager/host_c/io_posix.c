#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>
#include "adapters.h"

static am_result_t lock_store(void *context,bool acquire)
{
    am_host_t *h=context;
    if(!acquire) {
        if(h->lock_fd<0)return AM_IO;
        int r=flock(h->lock_fd,LOCK_UN),closed=close(h->lock_fd);h->lock_fd=-1;
        return r || closed?AM_IO:AM_OK;
    }
    if(h->lock_fd>=0)return AM_BUSY;
    char path[AM_VFS_PATH_MAX];int n=snprintf(path,sizeof path,"%s/.lock",h->vfs.root);
    if(n<0 || (size_t)n>=sizeof path)return AM_LIMIT;
    int fd=open(path,O_CREAT|O_RDWR,0600);if(fd<0)return AM_IO;
    if(flock(fd,LOCK_EX|LOCK_NB)){close(fd);return AM_BUSY;}
    h->lock_fd=fd;return AM_OK;
}
static am_result_t write_file(void *context,const char *path,size_t offset,const uint8_t *bytes,size_t size)
{
    am_host_t *h=context;
    if(h->write_full && !strcmp(path,"stage/module.elf") && offset>=512)return AM_FULL;
    return h->write_base(&h->vfs,path,offset,bytes,size);
}
static am_result_t checkpoint(void *context,am_checkpoint_t point)
{
    am_host_t *h=context;
    if(h->fault!=(int)point)return AM_OK;
    if(h->mode==2)_exit(73);
    return h->mode==1?AM_CANCELLED:AM_OK;
}
am_result_t am_host_io(am_host_t *h,const char *root,uint64_t quota,am_io_t *out)
{
    memset(h,0,sizeof *h);h->lock_fd=-1;h->fault=-1;
    am_result_t r=am_vfs_io(&h->vfs,root,quota,out);
    if(r!=AM_OK)return r;
    h->write_base=out->write;out->lock=lock_store;out->checkpoint=checkpoint;out->write=write_file;
    return AM_OK;
}
