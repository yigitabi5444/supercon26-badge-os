#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../txn_vfs.h"

static unsigned checks;
#define CHECK(e) do { assert(e);checks++; } while(0)
#ifndef ESP_PLATFORM
static void join(char out[AM_VFS_PATH_MAX],const char *root,const char *suffix)
{
    int n=snprintf(out,AM_VFS_PATH_MAX,"%s%s",root,suffix);
    CHECK(n>0 && n<AM_VFS_PATH_MAX);
}
static void expect(const char *path,const char *value)
{
    FILE *f=fopen(path,"rb");CHECK(f);char bytes[32]={0};
    CHECK(fread(bytes,1,sizeof bytes-1,f)==strlen(value));
    CHECK(!strcmp(bytes,value));CHECK(!fclose(f));
}
#endif
int main(int argc,char **argv)
{
    CHECK(argc==2);am_vfs_t vfs;am_io_t io;
    CHECK(am_vfs_io(&vfs,argv[1],65536,&io)==AM_OK);
    CHECK(io.write(io.context,"stage/a",0,(const uint8_t *)"first",5)==AM_OK);
    CHECK(io.move(io.context,"stage/a","stage/b",false)==AM_OK);
    CHECK(io.write(io.context,"stage/a",0,(const uint8_t *)"second",6)==AM_OK);
    CHECK(io.move(io.context,"stage/a","stage/b",false)==AM_IO); // Existing target preserved.
    uint8_t bytes[16];size_t size=0;
    CHECK(io.read(io.context,"stage/b",bytes,sizeof bytes,&size)==AM_OK);
    CHECK(size==5 && !memcmp(bytes,"first",5));
    CHECK(io.remove(io.context,"stage/a")==AM_OK);
    CHECK(io.mkdir(io.context,"stage/nested")==AM_OK);
    CHECK(io.write(io.context,"stage/nested/file",0,(const uint8_t *)"123",3)==AM_OK);
    uint64_t used=0,capacity=0;
    CHECK(io.space(io.context,&used,&capacity)==AM_OK && used==8 && capacity==65536);
    CHECK(io.remove(io.context,"stage/nested")==AM_OK);
    CHECK(io.remove(io.context,"stage/b")==AM_OK);
    CHECK(io.space(io.context,&used,&capacity)==AM_OK && used==0);
#ifndef ESP_PLATFORM
    char outside[AM_VFS_PATH_MAX],link[AM_VFS_PATH_MAX],directory[AM_VFS_PATH_MAX],sentinel[AM_VFS_PATH_MAX];
    join(outside,argv[1],"-outside");FILE *f=fopen(outside,"wb");CHECK(f);
    CHECK(fwrite("outside",1,7,f)==7);CHECK(!fclose(f));
    join(link,argv[1],"/stage/link");CHECK(!symlink(outside,link));
    CHECK(io.space(io.context,&used,&capacity)==AM_IO);
    CHECK(io.remove(io.context,"stage/link")==AM_IO);
    struct stat st;CHECK(!lstat(link,&st) && S_ISLNK(st.st_mode));expect(outside,"outside");
    join(directory,argv[1],"-directory");CHECK(!mkdir(directory,0700));
    join(sentinel,directory,"/sentinel");f=fopen(sentinel,"wb");CHECK(f);
    CHECK(fwrite("untouched",1,9,f)==9);CHECK(!fclose(f));
    join(link,argv[1],"/stage/linkdir");CHECK(!symlink(directory,link));
    CHECK(io.remove(io.context,"stage/linkdir")==AM_IO);
    CHECK(!lstat(link,&st) && S_ISLNK(st.st_mode));expect(sentinel,"untouched");
    join(outside,argv[1],"-missing");join(link,argv[1],"/stage/dangling");
    CHECK(!symlink(outside,link));
    CHECK(io.write(io.context,"stage/source",0,(const uint8_t *)"keep",4)==AM_OK);
    CHECK(io.move(io.context,"stage/source","stage/dangling",false)==AM_IO);
    CHECK(!lstat(link,&st) && S_ISLNK(st.st_mode));
    join(sentinel,argv[1],"/stage/source");expect(sentinel,"keep");
    printf("PASS %u host VFS assertions: file/dir operations, symlink walk/remove rejection, dangling-target no-replace\n",checks);
#else
    printf("PASS %u ESP-branch POSIX-model assertions: regular-file/directory operations, no lstat dependency\n",checks);
#endif
    return 0;
}
