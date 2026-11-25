// Actual namespace adapter with count-only mount/VFS doubles, no real filesystem.
#define main unused_guard_vectors
#include "test_littlefs_guard.c"
#undef main
#include <errno.h>
#include <sys/stat.h>

static int stat_error;
static int test_stat(const char *path,struct stat *out)
{
    assert(!strcmp(path,"/appstore/aliens") || !strcmp(path,"/appstore/rocks") ||
           !strcmp(path,"/appstore/codex_board") || !strcmp(path,"/appstore/hackaday"));
    if(stat_error){errno=stat_error;return -1;}
    memset(out,0,sizeof *out);out->st_mode=S_IFDIR;return 0;
}
#define stat(path,out) test_stat(path,out)
#include "../txn_littlefs.c"
#undef stat

int main(void)
{
    am_littlefs_t ctx;assert(am_littlefs_init(&ctx)==AM_OK);
    static am_workspace_t work;
    am_store_t store={.app_id="aliens",.work=&work,.runtime={"esp32s3","6.0.2","2.1.0",2},
        .crypto={.sha256=hash,.verify_p256=verify}};
    am_layout_policy_t policy=reset();mount_result=ESP_OK;io_result=AM_OK;
    assert(am_littlefs_open(&ctx,&policy,&store,NULL,NULL)==AM_OK);
    assert(!strcmp(ctx.vfs.root,"/appstore/aliens") && mounts==1 && writes==1);
    stat_error=ENOENT;
    assert(am_littlefs_select(&ctx,&store,"rocks",false)==AM_NOT_FOUND && writes==1);
    stat_error=EIO;
    assert(am_littlefs_select(&ctx,&store,"rocks",false)==AM_IO && writes==1);
    assert(!strcmp(ctx.vfs.root,"/appstore/aliens") && !strcmp(store.app_id,"aliens"));
    stat_error=0;
    am_lease_t lease={.owner=&store};store.lease=&lease;
    assert(am_littlefs_select(&ctx,&store,"rocks",true)==AM_BUSY && writes==1);
    store.lease=NULL;
    assert(store.io.lock(store.io.context,true)==AM_OK);
    assert(am_littlefs_select(&ctx,&store,"rocks",true)==AM_BUSY && writes==1);
    assert(store.io.lock(store.io.context,false)==AM_OK);
    assert(am_littlefs_select(&ctx,&store,"rocks",true)==AM_OK);
    assert(!strcmp(ctx.vfs.root,"/appstore/rocks") && !strcmp(store.app_id,"rocks"));
    assert(ctx.vfs.quota==policy.size && writes==2 && mounts==1 && !unmounts);
    assert(am_littlefs_select(&ctx,&store,"codex_board",false)==AM_OK && writes==3);
    assert(!strcmp(ctx.vfs.root,"/appstore/codex_board") && !strcmp(store.app_id,"codex_board"));
    assert(am_littlefs_select(&ctx,&store,"rocks",false)==AM_OK && writes==4);
    assert(am_littlefs_select(&ctx,&store,"../aliens",true)==AM_DISABLED && writes==4);
    io_result=AM_IO;
    assert(am_littlefs_select(&ctx,&store,"aliens",true)==AM_IO);
    assert(!strcmp(ctx.vfs.root,"/appstore/rocks") && !strcmp(store.app_id,"rocks"));
    assert(store.io.context==&ctx && mounts==1 && !unmounts);
    io_result=AM_OK;
    assert(am_littlefs_select(&ctx,&store,"aliens",true)==AM_OK);
    assert(!strcmp(ctx.vfs.root,"/appstore/aliens") && !strcmp(store.app_id,"aliens"));
    unsigned previous=writes;
    assert(am_littlefs_select(&ctx,&store,"hackaday",false)==AM_OK && writes==previous+1);
    assert(!strcmp(ctx.vfs.root,"/appstore/hackaday") && !strcmp(store.app_id,"hackaday"));
    assert(am_littlefs_close(&ctx)==AM_OK && unmounts==1);
    puts("PASS actual LittleFS selector: one mount, fixed namespaces, lease/mutex exclusion, atomic failure and original SpaceInv path; platform doubles only.");
}
