// Real controller/HTTPS/source/store/launcher/host, with SDK/loader doubles and exact host-compiled module.
#define main unused_runtime_checks
#include "test_video_runtime.c"
#undef main
#include <sys/stat.h>
#include "../controller.h"
#include "../video_source.h"
#include "esp_elf.h"
#include "video_https_double.h"

uint8_t *video_test_compressed(void);
uint8_t *video_test_pixels(void);
int video_test_main(int,char **);

typedef struct {
    am_host_t host;am_controller_t *controller;const char *root,*public;
    am_source_t source;am_video_source_config_t video;bool opened,committed;
    unsigned allocated,freed,launched;
    am_result_t (*lock_base)(void *,bool);
    am_result_t (*space_base)(void *,uint64_t *,uint64_t *);
} fixture_t;
static unsigned loader_inits,loader_deinits;
static uintptr_t (*resolver)(const char *);
const app_t *const apps[]={NULL};const int napps=0;
void icon_base(int x,int y){(void)x;(void)y;}
void app_manager_ui(void){assert(0);}
void elf_set_symbol_resolver(uintptr_t (*fn)(const char *))
{assert(!resolver);resolver=fn;assert(resolver("badge_app_get_api")==((uintptr_t)badge_app_get_api));assert(!resolver("stbi_zlib_decode_buffer"));}
void elf_reset_symbol_resolver(void){assert(resolver);resolver=NULL;}
int esp_elf_init(esp_elf_t *elf){assert(resolver);elf->initialized=1;loader_inits++;return 0;}
int esp_elf_relocate(esp_elf_t *elf,const uint8_t *data,size_t size)
{assert(elf->initialized && data && size==4880);return 0;}
int esp_elf_request(esp_elf_t *elf,int mode,int argc,char **argv)
{assert(elf->initialized && !mode && !argc && !argv);assert(video_test_main(0,NULL)==0);return 0;}
void esp_elf_deinit(esp_elf_t *elf){assert(elf->initialized);elf->initialized=0;loader_deinits++;}
static am_result_t volume_space(void *ctx,uint64_t *used,uint64_t *total)
{
    fixture_t *f=ctx;am_vfs_t volume={.quota=2*1024*1024};assert(strlen(f->root)<sizeof volume.root);
    strcpy(volume.root,f->root);return f->space_base(&volume,used,total);
}
static am_result_t lock_io(void *ctx,bool acquire)
{
    fixture_t *f=ctx;am_result_t r=f->lock_base(ctx,acquire);
    if(r==AM_OK){vh.locked=acquire;if(acquire)f->committed=false;}
    return r;
}
static am_result_t checkpoint_io(void *ctx,am_checkpoint_t point)
{
    fixture_t *f=ctx;
    if(point==AM_AFTER_ACTIVATE || point==AM_AFTER_UNINSTALL)f->committed=true;
    return !f->committed && am_controller_cancel(f->controller)?AM_CANCELLED:AM_OK;
}
static am_result_t bind(fixture_t *f,am_store_t *s,const char *id)
{
    char path[AM_VFS_BASE_MAX];int n=snprintf(path,sizeof path,"%s/%s",f->root,id);
    if(n<0 || (size_t)n>=sizeof path)return AM_LIMIT;
    am_result_t r=am_host_io(&f->host,path,2*1024*1024,&s->io);
    if(r==AM_OK){f->space_base=s->io.space;f->lock_base=s->io.lock;s->io.space=volume_space;s->io.lock=lock_io;s->io.checkpoint=checkpoint_io;}
    return r;
}
static am_result_t select_id(void *ctx,am_store_t *s,const char *id,bool create,bool *ready)
{
    fixture_t *f=ctx;*ready=!strcmp(id,"video") && am_video_source_valid(&f->video);
    if(!f->opened)return AM_OK;
    if(!create) {
        char path[AM_VFS_BASE_MAX];struct stat st;int n=snprintf(path,sizeof path,"%s/%s",f->root,id);
        if(n<0 || (size_t)n>=sizeof path)return AM_LIMIT;
        if(stat(path,&st))return AM_NOT_FOUND;
    }
    am_result_t r=bind(f,s,id);
    if(r==AM_OK){f->source.app_id=id;f->source.valid=false;}
    return r;
}
static am_result_t open_store(void *ctx,am_store_t *s,bool (*cancel)(void *),void *owner)
{
    (void)cancel;(void)owner;fixture_t *f=ctx;am_result_t r=bind(f,s,s->app_id);
    if(r==AM_OK)r=am_openssl_crypto(f->public,&s->crypto);
    f->opened=r==AM_OK;return r;
}
static am_result_t close_store(void *ctx)
{fixture_t *f=ctx;assert(f->host.lock_fd==-1);am_openssl_close(&f->controller->store.crypto);return AM_OK;}
static am_result_t incoming(void *ctx,am_package_t *out){return am_source_cached(&((fixture_t *)ctx)->source,out);}
static am_result_t fetch_code(void *ctx,bool (*cancel)(void *),void *owner)
{
    fixture_t *f=ctx;assert(!vh.deferred && !vh.locked);
    if(!f->source.bytes)f->source.bytes=calloc(1,sizeof *f->source.bytes);assert(f->source.bytes);
    am_https_t http;am_result_t r=am_https_begin(&http,f->source.config,cancel,owner,NULL);
    if(r==AM_OK)r=am_source_fetch(&f->source,&f->controller->store.crypto,&f->controller->store.runtime,
                                 am_https_get,&http,cancel,owner);
    am_https_end(&http);return r;
}
static am_result_t prepare_asset(void *ctx,am_store_t *s,bool (*cancel)(void *),void *owner)
{
    fixture_t *f=ctx;if(strcmp(s->app_id,"video"))return AM_OK;
    assert(vh.deferred && !vh.locked);
    am_video_store_t store={s,am_openssl_sha256_stream};am_https_t http;
    am_result_t r=am_video_source_prepare(&store,&f->video,&http,cancel,owner,NULL);
    if(r!=AM_OK && r!=AM_UP_TO_DATE)f->source.valid=false;
    return r;
}
static void discard(void *ctx){fixture_t *f=ctx;free(f->source.bytes);f->source.bytes=NULL;f->source.valid=false;}
static void *allocate(void *ctx,size_t size,bool execution)
{(void)execution;fixture_t *f=ctx;void *p=malloc(size);if(p)f->allocated++;return p;}
static void release(void *ctx,void *p){((fixture_t *)ctx)->freed++;free(p);}
static void home_mode(void *ctx,bool on){(void)ctx;vh.deferred=on;}
static bool home_request(void *ctx){(void)ctx;bool value=vh.cancelled;vh.cancelled=false;return value;}
static bool home_end(void *ctx){vh.deferred=false;return home_request(ctx);}
static am_result_t launch_video(void *ctx,const uint8_t *copy,size_t size,const am_metadata_t *meta)
{
    fixture_t *f=ctx;assert(!vh.clients && !vh.radio && !vh.deferred && vh.locked);
    am_video_store_t store={&f->controller->store,am_openssl_sha256_stream};
    am_native_video_t binding={.inflate=am_native_video_inflate,.info=am_native_video_info};
    am_result_t r=am_video_asset_open(&store,&f->controller->lease,AM_VIDEO_CLIP_HASH,&binding.asset);
    if(r!=AM_OK)return r;
    nh_reset();register_memory();nh_memory(video_test_compressed(),BADGE_VIDEO_COMPRESSED_MAX,NH_PSRAM,true);
    nh_memory(video_test_pixels(),BADGE_VIDEO_PIXELS,NH_PSRAM,true);nh_auto_b_frame=3;
    r=app_manager_launch_video_snapshot(copy,size,meta,&binding);if(r==AM_OK)f->launched++;return r;
}
static am_result_t quiesce(void *ctx){(void)ctx;return app_manager_quiesce_snapshot();}
int main(int argc,char **argv)
{
    assert(argc==8); // root public raw-key code-dir asset-dir action response-mode
    nh_reset();fixture_t f={.root=argv[1],.public=argv[2]};am_controller_t controller;f.controller=&controller;
    uint8_t key[65];FILE *file=fopen(argv[3],"rb");assert(file);assert(fread(key,1,65,file)==65 && fgetc(file)==EOF);fclose(file);
    static const char ca[]="controlled-public-CA-double";
    am_source_config_t config={.manifest_url="https://192.0.2.1:8443/video/manifest.json",
        .signature_url="https://192.0.2.1:8443/video/manifest.sig",.payload_url="https://192.0.2.1:8443/video/module.elf",
        .public_key=key,.key_size=65,.local_authority="192.0.2.1:8443",.tls_ca_pem=ca};
    f.source.config=&config;
    f.video=(am_video_source_config_t){&config,"https://192.0.2.1:8443/video/clip.receipt",
        "https://192.0.2.1:8443/video/clip.sig","https://192.0.2.1:8443/video/clip.vid"};
    am_video_source_config_t wrong=f.video;wrong.clip_url="https://other.example/clip.vid";
    assert(!am_video_source_valid(&wrong));wrong=f.video;wrong.receipt_url=NULL;assert(!am_video_source_valid(&wrong));
    am_source_config_t public_config=config;public_config.local_authority=public_config.tls_ca_pem=NULL;
    public_config.manifest_url="https://packages.example/video/manifest.json";
    public_config.signature_url="https://packages.example/video/manifest.sig";
    public_config.payload_url="https://packages.example/video/module.elf";
    wrong=(am_video_source_config_t){&public_config,"https://packages.example/video/clip.receipt",
        "https://packages.example/video/clip.sig","https://packages.example/video/clip.vid"};
    assert(am_video_source_valid(&wrong));wrong.clip_url="https://other.example/clip.vid";
    assert(!am_video_source_valid(&wrong));
    vh=(video_https_fixture_t){.config=&config,.code=argv[4],.assets=argv[5],.mode=argv[7]};
    am_platform_t platform={.context=&f,.assigned=true,.open=open_store,.close=close_store,.incoming=incoming,
        .fetch=fetch_code,.discard=discard,.allocate=allocate,.free=release,.home_mode=home_mode,
        .home_request=home_request,.home_end=home_end,.launch=launch_video,.quiesce=quiesce,
        .select=select_id,.prepare=prepare_asset};
    am_result_t r=am_controller_init(&controller,platform,(am_runtime_t){"esp32s3","6.0.2","2.1.0",2});
    unsigned id=0;for(;id<AM_COUNT;id++)if(!strcmp(am_catalogue[id].id,"video"))break;assert(id<AM_COUNT);
    if(r==AM_OK)r=am_controller_action(&controller,id,!strcmp(argv[6],"open")?AM_OPEN:
        !strcmp(argv[6],"update")?AM_UPDATE:!strcmp(argv[6],"uninstall")?AM_UNINSTALL:AM_INSTALL);
    bool installed=controller.local[id].installed,cached=controller.local[id].package_available;
    assert(am_controller_shutdown(&controller)==AM_OK && f.allocated==f.freed && !vh.locked && !vh.deferred);
    assert(!vh.clients && !vh.radio && vh.opens==vh.cleans && !am_native_host_live() && !resolver);
    assert(loader_inits==loader_deinits);
    printf("{\"result\":%d,\"installed\":%s,\"cached\":%s,\"launched\":%u,\"opens\":%u,\"clip_opens\":%u,\"clip_reads\":%u,\"max_read\":%u,\"joins\":%u,\"syncs\":%u,\"loader_inits\":%u}\n",
        r,installed?"true":"false",cached?"true":"false",f.launched,vh.opens,vh.clip_opens,vh.clip_reads,
        vh.max_read,vh.joins,vh.syncs,loader_inits);
}

int esp_elf_relocate_source(esp_elf_t *e,const elf_source_t *s) {(void)e;(void)s;assert(0);return -1;}

size_t am_home_apps_inventory(const app_t **out,size_t capacity)
{(void)out;(void)capacity;assert(!"Home provider is outside this sealed app fixture");return 0;}
