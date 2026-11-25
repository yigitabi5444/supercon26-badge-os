#define _POSIX_C_SOURCE 200809L
#include "../txn_vfs.h"
#define main unused_guard_vectors
#define am_vfs_io unused_guard_vfs
#define am_vfs_readonly_io unused_guard_readonly_vfs
#define esp_vfs_littlefs_register guard_mount
#include "test_littlefs_guard.c"
#undef main
#undef am_vfs_io
#undef am_vfs_readonly_io
#undef esp_vfs_littlefs_register
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../controller_target.h"
#include "../home_apps.c"
#include "../host_c/adapters.h"
#include "../txn_crypto_psa.h"
#include "../native_codex.h"
#include "../native_video.h"
#include "../native_hackaday.h"
#include "../native_messages.h"
#include "../native_doom.h"
#include "esp_elf.h"
#include "home_fixture_config.h"
static const app_t core[]={{.name="Badge"},{.name="Clock"},{.name="Update"},{.name="Multi-pong"},
    {.name="Snake"},{.name="Life"},{.name="Tetris"},{.name="Radar"},{.name="Source"},{.name="Weather"},
    {.name="Settings"},{.name="FAQ"},{.name="Clock Faces"}};
extern const app_t app_app_manager;
const app_t *const apps[]={&core[0],&core[1],&core[2],&core[3],&core[4],&core[5],&core[6],
    &core[7],&core[8],&core[9],&core[10],&core[11],&core[12],&app_app_manager};
const int napps=14;
const app_t app_aliens={.name="Legacy alias"}; // Pointer identity still deduplicates a renamed stock descriptor.
const app_t app_codex_board={0},app_messaging={0},app_doom={0},app_rocks={0},app_hackaday={0},app_video={0};
static const char *volume;
static bool scanning,readonly_mount,host_live,resolver_live,deferred;
static unsigned readonly_mounts,normal_mounts,filesystem_writes,network_calls,launches,deinits,stops,polls,allocs,frees;
static am_crypto_t real_crypto;
static void mapped_path(const char *path,char out[AM_VFS_PATH_MAX])
{
    int n=snprintf(out,AM_VFS_PATH_MAX,"%s%s",!strncmp(path,"/appstore",9)?volume:"",!strncmp(path,"/appstore",9)?path+9:path);
    assert(n>0 && n<AM_VFS_PATH_MAX);
}
int home_test_stat(const char *path,struct stat *out)
{char p[AM_VFS_PATH_MAX];mapped_path(path,p);return stat(p,out);}
am_result_t home_test_vfs(am_vfs_t *v,const char *path,uint64_t quota,am_io_t *out)
{assert(!scanning && !readonly_mount);char p[AM_VFS_PATH_MAX];mapped_path(path,p);return am_vfs_io(v,p,quota,out);}
am_result_t home_test_readonly_vfs(am_vfs_t *v,const char *path,uint64_t quota,am_io_t *out)
{assert(scanning && readonly_mount);char p[AM_VFS_PATH_MAX];mapped_path(path,p);return am_vfs_readonly_io(v,p,quota,out);}
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *c)
{assert(c->read_only==scanning);readonly_mount=c->read_only;if(scanning)readonly_mounts++;else normal_mounts++;return guard_mount(c);}
int home_test_mkdir(const char *p,mode_t mode){assert(!scanning);filesystem_writes++;return mkdir(p,mode);}
int home_test_unlink(const char *p){assert(!scanning);filesystem_writes++;return unlink(p);}
int home_test_rename(const char *a,const char *b){assert(!scanning);filesystem_writes++;return rename(a,b);}
int home_test_fsync(int fd){assert(!scanning);filesystem_writes++;return fsync(fd);}
ssize_t home_test_write(int fd,const void *p,size_t n){assert(!scanning);filesystem_writes++;return write(fd,p,n);}
int home_test_open(const char *p,int flags,...)
{
    if(scanning)assert(!(flags&(O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|O_APPEND)));
    if(flags&O_CREAT){va_list a;va_start(a,flags);int mode=va_arg(a,int);va_end(a);return open(p,flags,mode);}
    return open(p,flags);
}
void *heap_caps_aligned_alloc(size_t alignment,size_t n,unsigned caps)
{(void)alignment;assert(caps==3);void *p=malloc(n);if(p)allocs++;return p;}
void heap_caps_free(void *p){if(p){frees++;free(p);}}
am_result_t am_psa_adapter(am_psa_context_t *c,const uint8_t *key,size_t n,am_crypto_t *out)
{assert(n==65 && !memcmp(key,test_public,65));memcpy(c->public_key,key,n);*out=real_crypto;return AM_OK;}
am_result_t am_psa_sha256_stream(am_reader_t r,void *c,uint32_t n,uint8_t out[32])
{return am_openssl_sha256_stream(r,c,n,out);}
void btn_home_prompt(bool on){assert(!scanning);deferred=on;}
bool btn_home_requested(void){assert(!scanning);return false;}
bool btn_home_prompt_end(void){assert(!scanning);deferred=false;return false;}
int btn_get(int ms){assert(!scanning && ms==20 && host_live && resolver_live && !deinits);polls++;return -1;}
int btn_held(void){return 0;}
void btn_flush(void){}
void ui_keys(const char *p,const char *m,const char *a,const char *b){(void)p;(void)m;(void)a;(void)b;assert(!scanning && !host_live);}
void box(int x,int y,int w,int h,bool ink){(void)x;(void)y;(void)w;(void)h;(void)ink;assert(!scanning && !host_live);}
void text(int x,int y,int scale,const char *s,bool ink){(void)x;(void)y;(void)scale;(void)s;(void)ink;assert(!scanning && !host_live);}
void ui_frame(int n){(void)n;assert(!scanning && !host_live);}
void ui_close(void){assert(!scanning && !host_live);}
void icon_base(int x,int y){(void)x;(void)y;assert(!scanning);}
bool wifi_connected(void){return false;}
bool am_https_enabled(void){return true;}
bool am_https_clock_ready(void){return true;}
static am_result_t no_network(void){network_calls++;assert(!"Home scan/Open must be offline");return AM_IO;}
am_result_t am_https_prepare(const am_source_config_t *c,bool (*x)(void *),void *o,am_progress_t p)
{(void)c;(void)x;(void)o;(void)p;return no_network();}
am_result_t am_https_begin(am_https_t *h,const am_source_config_t *c,bool (*x)(void *),void *o,am_progress_t p)
{(void)h;(void)c;(void)x;(void)o;(void)p;return no_network();}
void am_https_end(am_https_t *h){(void)h;no_network();}
am_result_t am_https_get(void *h,const char *u,uint8_t *p,size_t n,size_t *got)
{(void)h;(void)u;(void)p;(void)n;(void)got;return no_network();}
am_result_t am_https_doom_get(void *h,const char *u,uint8_t *p,size_t n,size_t *got)
{return am_https_get(h,u,p,n,got);}
const am_video_source_config_t am_video_source_config={0};
bool am_video_source_valid(const am_video_source_config_t *v){(void)v;return false;}
am_result_t am_video_source_prepare(am_video_store_t *s,const am_video_source_config_t *v,am_https_t *h,bool (*c)(void *),void *o,am_progress_t p)
{(void)s;(void)v;(void)h;(void)c;(void)o;(void)p;return no_network();}
am_result_t am_video_asset_open(am_video_store_t *s,const am_lease_t *l,const char *h,am_video_asset_t *a)
{(void)s;(void)l;(void)h;(void)a;assert(0);return AM_DISABLED;}
int32_t am_native_video_inflate(uint8_t *o,uint32_t n,const uint8_t *p,uint32_t m){(void)o;(void)n;(void)p;(void)m;assert(0);return -1;}
bool am_native_video_info(void){assert(0);return false;}
const am_native_codex_t am_native_codex_target={0};
const am_native_hackaday_t am_native_hackaday_target={0};
const am_native_messages_t am_native_messages_target={0};
am_result_t am_native_doom_target_launch(am_store_t *s,const am_lease_t *l,const am_metadata_t *m,const char *p)
{(void)s;(void)l;(void)m;(void)p;assert(0);return AM_DISABLED;}
am_result_t am_native_host_start(void){assert(0);return AM_DISABLED;}
am_result_t am_native_host_start_for(const char *id)
{assert(!scanning && !strcmp(id,"aliens") && resolver_live && !host_live);host_live=true;launches++;return AM_OK;}
#define UNUSED_TYPED(name,type) am_result_t am_native_host_start_##name(const type *p){(void)p;assert(0);return AM_DISABLED;}
UNUSED_TYPED(video,am_native_video_t) UNUSED_TYPED(codex,am_native_codex_t) UNUSED_TYPED(hackaday,am_native_hackaday_t)
UNUSED_TYPED(messages,am_native_messages_t) UNUSED_TYPED(doom,am_native_doom_t)
am_result_t am_native_host_stop(void)
{assert(!scanning);if(host_live && ++stops<3)return AM_BUSY;host_live=false;return AM_OK;}
bool am_native_host_live(void){return host_live;}
bool am_native_host_good_return(void){assert(host_live);return true;}
const badge_native_api_t *badge_app_get_api(uint32_t a,uint32_t n){(void)a;(void)n;return NULL;}
void elf_set_symbol_resolver(uintptr_t (*r)(const char *))
{assert(!resolver_live && r("badge_app_get_api")==((uintptr_t)badge_app_get_api) && !r("malloc"));resolver_live=true;}
void elf_reset_symbol_resolver(void){assert(resolver_live && !host_live);resolver_live=false;}
int esp_elf_init(esp_elf_t *e){assert(!scanning && resolver_live);e->initialized=1;return 0;}
int esp_elf_relocate(esp_elf_t *e,const uint8_t *p,size_t n){assert(e->initialized && p && n==4928);return 0;}
int esp_elf_relocate_source(esp_elf_t *e,const elf_source_t *s){(void)e;(void)s;assert(0);return -1;}
int esp_elf_request(esp_elf_t *e,int mode,int argc,char **argv)
{assert(e->initialized && host_live && !mode && !argc && !argv);return 0;}
void esp_elf_deinit(esp_elf_t *e){assert(e->initialized && !host_live);deinits++;e->initialized=0;}
static size_t inventory(const app_t **out)
{
    unsigned before=filesystem_writes;scanning=true;size_t n=app_manager_home_apps(out,7);scanning=false;
    assert(filesystem_writes==before && allocs==frees && !network_calls && !deferred);return n;
}
static void seed(const char *assets)
{
    am_workspace_t *work=calloc(1,sizeof *work);assert(work);am_host_t host;am_store_t s={.app_id="aliens",.runtime={"esp32s3","6.0.2","2.1.0",2},.work=work,.crypto=real_crypto};
    char root[AM_VFS_PATH_MAX];snprintf(root,sizeof root,"%s/aliens",volume);assert(am_host_io(&host,root,0x100000,&s.io)==AM_OK);
    uint8_t manifest[4096],sig[128],elf[65536];size_t sizes[3];uint8_t *buffers[]={manifest,sig,elf};const size_t caps[]={4096,128,65536};
    const char *names[]={"manifest.json","manifest.sig","module.elf"};
    for(unsigned i=0;i<3;i++){char path[512];snprintf(path,sizeof path,"%s/%s",assets,names[i]);FILE *f=fopen(path,"rb");assert(f);sizes[i]=fread(buffers[i],1,caps[i],f);assert(feof(f) && !fclose(f));}
    assert(am_store_install(&s,(am_package_t){{manifest,sizes[0]},{sig,sizes[1]},{elf,sizes[2]}})==AM_OK);free(work);
}
int main(int argc,char **argv)
{
    assert(argc==4);volume=argv[1];assert(am_openssl_crypto(argv[2],&real_crypto)==AM_OK);reset();mount_result=ESP_OK;seed(argv[3]);
    char unfinished[AM_VFS_PATH_MAX];snprintf(unfinished,sizeof unfinished,"%s/aliens/stage/unfinished",volume);
    FILE *pending=fopen(unfinished,"wb");assert(pending && fputs("preserve during Home scan",pending)>=0 && !fclose(pending));
    const app_t named={.name="SpaceInv"},id_named={.name="aliens"};assert(same_app(&app_aliens,3) && same_app(&named,3) && same_app(&id_named,3));
    const app_t *out[7];unsigned before=mounts;assert(!app_manager_home_apps(NULL,0) && mounts==before);
    assert(inventory(out)==1 && !strcmp(out[0]->name,"SpaceInv"));for(unsigned i=1;i<7;i++)assert(!out[i]);
    const app_t *stable=out[0];assert(inventory(out)==1 && out[0]==stable);
    assert(!access(unfinished,F_OK)); // Inventory did not run recovery or clean the abandoned stage.
    char path[AM_VFS_PATH_MAX],record[512];snprintf(path,sizeof path,"%s/aliens/active/app.json",volume);
    FILE *f=fopen(path,"rb");assert(f);size_t n=fread(record,1,sizeof record,f);assert(!fclose(f));am_pointer_t active;
    assert(am_pointer_parse((am_bytes_t){(const uint8_t *)record,n},&active)==AM_OK);
    snprintf(path,sizeof path,"%s/aliens/versions/%s/module.elf",volume,active.current);
    f=fopen(path,"r+b");assert(f && !fseek(f,100,SEEK_SET));int old=fgetc(f);assert(old!=EOF && !fseek(f,100,SEEK_SET));
    assert(fputc(old^1,f)!=EOF && !fclose(f));assert(inventory(out)==0 && !out[0]); // Corruption never becomes a shortcut.
    f=fopen(path,"r+b");assert(f && !fseek(f,100,SEEK_SET) && fputc(old,f)!=EOF && !fclose(f));
    mount_result=ESP_FAIL;assert(inventory(out)==0 && !out[0]);mount_result=ESP_OK;
    assert(inventory(out)==1);out[0]->run();assert(launches==1 && deinits==1 && stops==3 && polls==1 && !host_live && !resolver_live && allocs==frees);
    assert(access(unfinished,F_OK) && errno==ENOENT); // Normal authenticated Open retained its existing recovery path.
    am_controller_t c;assert(am_target_start(&c)==AM_OK);assert(am_controller_action(&c,3,AM_UNINSTALL)==AM_OK);assert(am_controller_shutdown(&c)==AM_OK);
    assert(inventory(out)==0);for(unsigned i=0;i<7;i++)assert(!out[i]);
    assert(readonly_mounts==6 && normal_mounts==2 && mounts==8 && unmounts==7 && allocs==frees && !network_calls);
    am_openssl_close(&real_crypto);puts("PASS installed-only Home provider: genuine signed SpaceInv inventory and normal authenticated Open, read-only/no-create scan, no network/recovery, stable names/descriptors and stock dedup, busy-safe cleanup, uninstall refresh removes stale shortcut; SDK/loader doubled, no shared Home/device");
}
