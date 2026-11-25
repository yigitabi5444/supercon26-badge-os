// Genuine signed selected-ID source/store path, then actual module through the real host.
#define main unused_controller_vectors
#include "native_controller_driver.c"
#undef main
#include "native_codex.h"
#include "badge/badge.h"
#include "esp_elf.h"
#include "native_host_double.h"
#include "codex_sdk_double.h"
void cb_test_prepare_module(bool);
void cb_test_assert_clean(void);
int cb_test_main(int,char **);
static bool lan;
static unsigned loader_inits,loader_deinits;
static uintptr_t (*resolver)(const char *);
const app_t *const apps[]={NULL};const int napps=0;
void icon_base(int x,int y){(void)x;(void)y;}
void app_manager_ui(void){assert(0);}
void elf_set_symbol_resolver(uintptr_t (*fn)(const char *))
{assert(!resolver);resolver=fn;assert(resolver("badge_app_get_api")==((uintptr_t)badge_app_get_api));assert(!resolver("usb_serial_jtag_read_bytes"));}
void elf_reset_symbol_resolver(void){assert(resolver);resolver=NULL;}
int esp_elf_init(esp_elf_t *elf){assert(resolver);elf->initialized=1;loader_inits++;return 0;}
int esp_elf_relocate(esp_elf_t *elf,const uint8_t *bytes,size_t size)
{assert(elf->initialized && bytes && size==13240);return 0;}
int esp_elf_request(esp_elf_t *elf,int mode,int argc,char **argv)
{assert(elf->initialized && !mode && !argc && !argv);return cb_test_main(0,NULL);}
void esp_elf_deinit(esp_elf_t *elf){assert(elf->initialized);elf->initialized=0;loader_deinits++;}
static am_result_t launch_codex(void *ctx,const uint8_t *copy,size_t size,const am_metadata_t *meta)
{
    fixture_t *f=ctx;assert(!f->deferred && f->controller->lease.owner && f->host.lock_fd>=0);
    assert(!strcmp(meta->id,"codex_board") && size==meta->size);
    cb_test_prepare_module(lan);
    am_result_t r=app_manager_launch_codex_snapshot(copy,size,meta,&am_native_codex_target);
    assert(r==AM_OK && am_native_host_good_return());cb_test_assert_clean();f->launched++;return r;
}
static am_result_t stop_host(void *ctx){(void)ctx;return app_manager_quiesce_snapshot();}
int main(int argc,char **argv)
{
    assert(argc==8); // root public raw-public assets action selected-ID USB/LAN
    fixture_t f={.root=argv[1],.key=argv[2],.folder=argv[4],.multiple=true};am_controller_t c;f.controller=&c;
    uint8_t key[65];FILE *file=fopen(argv[3],"rb");assert(file && fread(key,1,65,file)==65 && fgetc(file)==EOF);fclose(file);
    const am_source_config_t config={.public_key=key,.key_size=65,
        .manifest_url="https://packages.example/manifest.json",.signature_url="https://packages.example/manifest.sig",
        .payload_url="https://packages.example/module.elf"};
    f.online=!strcmp(argv[5],"install") || !strcmp(argv[5],"update");
    f.source.config=&config;f.source.bytes=calloc(1,sizeof *f.source.bytes);assert(f.source.bytes);
    f.input=calloc(1,sizeof *f.input);assert(f.input);lan=!strcmp(argv[7],"LAN");
    const am_platform_t p={.context=&f,.assigned=true,.open=open_store,.close=close_store,.select=select_app,
        .incoming=incoming,.fetch=fetch,.allocate=allocate,.free=release,.home_mode=mode,.home_request=requested,
        .home_end=end,.launch=launch_codex,.quiesce=stop_host};
    am_result_t r=am_controller_init(&c,p,(am_runtime_t){"esp32s3","6.0.2","2.1.0",2});
    unsigned id=0;for(;id<AM_COUNT;id++)if(!strcmp(am_catalogue[id].id,argv[6]))break;assert(id<AM_COUNT);
    if(r==AM_OK)r=am_controller_action(&c,id,!strcmp(argv[5],"open")?AM_OPEN:
        !strcmp(argv[5],"update")?AM_UPDATE:!strcmp(argv[5],"uninstall")?AM_UNINSTALL:AM_INSTALL);
    bool installed=c.local[id].installed;unsigned payload=f.payload_reads,fetches=f.fetched;
    assert(am_controller_shutdown(&c)==AM_OK && !c.execution_copy && !c.lease.owner && !f.deferred);
    assert(f.allocated==f.freed && !resolver && loader_inits==loader_deinits);
    printf("{\"result\":%d,\"installed\":%s,\"payload_reads\":%u,\"fetches\":%u,\"launched\":%u,\"loaders\":%u}\n",
        r,installed?"true":"false",payload,fetches,f.launched,loader_inits);
    free(f.input);free(f.source.bytes);
}

int esp_elf_relocate_source(esp_elf_t *e,const elf_source_t *s) {(void)e;(void)s;assert(0);return -1;}

size_t am_home_apps_inventory(const app_t **out,size_t capacity)
{(void)out;(void)capacity;assert(!"Home provider is outside this sealed app fixture");return 0;}
