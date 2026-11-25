#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define main unused_transaction_vectors
#include "native_controller_driver.c"
#undef main
#include <pthread.h>
#if defined(__APPLE__)
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#endif
#include "badge/badge.h"
#include "native_messages.h"
#include "native_video.h"
#include "native_host_double.h"
#include "messages_sdk_double.h"
#include "esp_elf.h"
static fixture_t *running;
static unsigned loader_inits,loader_deinits;
static uintptr_t (*resolver)(const char *);
const app_t *const apps[]={NULL};const int napps=0;
void icon_base(int x,int y){(void)x;(void)y;}
void app_manager_ui(void){assert(0);}
int messages_test_main(int,char **);
void messages_test_memory(void);
am_result_t am_video_asset_read(const am_video_asset_t *a,uint32_t o,uint8_t *p,size_t n)
{(void)a;(void)o;(void)p;(void)n;assert(0);return AM_DISABLED;}
bool am_video_asset_live(const am_video_asset_t *a){(void)a;assert(0);return false;}
void elf_set_symbol_resolver(uintptr_t (*fn)(const char *))
{assert(!resolver);resolver=fn;assert(resolver("badge_app_get_api")==((uintptr_t)badge_app_get_api));assert(!resolver("messaging_snapshot"));}
void elf_reset_symbol_resolver(void){assert(resolver);resolver=NULL;}
int esp_elf_init(esp_elf_t *elf){assert(resolver);elf->initialized=1;loader_inits++;return 0;}
int esp_elf_relocate(esp_elf_t *elf,const uint8_t *data,size_t size)
{assert(elf->initialized && data && size==17216);return 0;}
int esp_elf_request(esp_elf_t *elf,int mode,int argc,char **argv)
{assert(elf->initialized && !mode && !argc && !argv);assert(messages_test_main(0,NULL)==0);return 0;}
void esp_elf_deinit(esp_elf_t *elf){assert(elf->initialized);elf->initialized=0;loader_deinits++;}
static void register_memory(void)
{
#if defined(__APPLE__)
    size_t n=pthread_get_stacksize_np(pthread_self());uintptr_t high=(uintptr_t)pthread_get_stackaddr_np(pthread_self());
    nh_memory((void *)(high-n),n,NH_DRAM,true);
    const char *segments[]={"__TEXT","__TEXT","__TEXT","__DATA","__DATA_CONST"};
    const char *sections[]={"__cstring","__asan_cstring","__const","__const","__const"};
    for(unsigned i=0;i<5;i++){unsigned long size=0;const uint8_t *p=getsectiondata(&_mh_execute_header,segments[i],sections[i],&size);
        if(p && size)nh_memory(p,size,NH_DROM,false);}
#else
    uintptr_t here=(uintptr_t)&here;nh_memory((void *)(here-65536),131072,NH_DRAM,true);
    extern char __executable_start,etext;nh_memory(&__executable_start,(size_t)(&etext-&__executable_start),NH_DROM,false);
#endif
    messages_test_memory();memset(fb,0xa5,TOP*(W/8));
}
void messages_test_home(void)
{
    fixture_t *f=running;assert(f && f->controller->execution_copy && f->controller->lease.owner && resolver);
    messages_sdk_phone_check();assert(messages_sdk.reads && messages_sdk.reader_keys==5);
    const badge_messages_api_t *api=(const void *)badge_app_get_api(2,sizeof *api);assert(api && !api->release(api->base.context));
    assert(app_manager_quiesce_snapshot()==AM_BUSY && am_native_host_live() && resolver && !loader_deinits);
    assert(f->controller->execution_copy && f->controller->lease.owner && !atomic_load(&nh_tasks_deleted));
    for(unsigned i=0;i<TOP*(W/8);i++)assert(fb[i]==0xa5);
    printf("{\"result\":9,\"reset\":true,\"pinned\":true,\"reads\":%u,\"servers\":%u}\n",messages_sdk.reads,messages_sdk.servers);
    fflush(stdout);exit(0); // Existing global Home/reset is modeled as whole-process termination, never fake cleanup.
}
static am_result_t launch_messages(void *ctx,const uint8_t *copy,size_t size,const am_metadata_t *meta)
{
    fixture_t *f=ctx;assert(!f->deferred && f->controller->lease.owner && f->host.lock_fd>=0 && !strcmp(meta->id,"messaging"));
    nh_reset();register_memory();running=f;
    am_result_t r=app_manager_launch_messages_snapshot(copy,size,meta,&am_native_messages_target);
    assert(r==AM_OK && !messages_sdk.available && am_native_host_good_return() && !messages_sdk.servers && !nh_resident_tasks);
    f->launched++;return r;
}
static am_result_t stop_host(void *ctx){(void)ctx;return app_manager_quiesce_snapshot();}
int main(int argc,char **argv)
{
    assert(argc==9); // root public raw-key assets action mode resident-message-store.dat ID
    messages_sdk_init(argv[7],argv[6]);
    fixture_t f={.root=argv[1],.key=argv[2],.folder=argv[4],.multiple=true};am_controller_t c;f.controller=&c;
    uint8_t key[65];FILE *file=fopen(argv[3],"rb");assert(file && fread(key,1,65,file)==65 && fgetc(file)==EOF);fclose(file);
    const am_source_config_t config={.public_key=key,.key_size=65,
        .manifest_url="https://packages.example/manifest.json",.signature_url="https://packages.example/manifest.sig",
        .payload_url="https://packages.example/module.elf"};
    f.online=!strcmp(argv[5],"install") || !strcmp(argv[5],"update");
    f.source.config=&config;f.source.bytes=calloc(1,sizeof *f.source.bytes);assert(f.source.bytes);
    f.input=calloc(1,sizeof *f.input);assert(f.input);
    const am_platform_t p={.context=&f,.assigned=true,.open=open_store,.close=close_store,.select=select_app,
        .incoming=incoming,.fetch=fetch,.allocate=allocate,.free=release,.home_mode=mode,.home_request=requested,
        .home_end=end,.launch=launch_messages,.quiesce=stop_host};
    am_result_t r=am_controller_init(&c,p,(am_runtime_t){"esp32s3","6.0.2","2.1.0",2});
    unsigned id=0;for(;id<AM_COUNT;id++)if(!strcmp(am_catalogue[id].id,argv[8]))break;assert(id<AM_COUNT);
    if(r==AM_OK)r=am_controller_action(&c,id,!strcmp(argv[5],"open")?AM_OPEN:
        !strcmp(argv[5],"update")?AM_UPDATE:!strcmp(argv[5],"uninstall")?AM_UNINSTALL:AM_INSTALL);
    bool installed=c.local[id].installed;
    assert(am_controller_shutdown(&c)==AM_OK && !c.execution_copy && !c.lease.owner && !f.deferred);
    assert(f.allocated==f.freed && !resolver && loader_inits==loader_deinits);
    assert(atomic_load(&nh_tasks_created)==atomic_load(&nh_tasks_deleted));
    printf("{\"result\":%d,\"installed\":%s,\"launched\":%u,\"reset\":false}\n",r,installed?"true":"false",f.launched);
    free(f.input);free(f.source.bytes);return 0;
}

int esp_elf_relocate_source(esp_elf_t *e,const elf_source_t *s) {(void)e;(void)s;assert(0);return -1;}

size_t am_home_apps_inventory(const app_t **out,size_t capacity)
{(void)out;(void)capacity;assert(!"Home provider is outside this sealed app fixture");return 0;}
