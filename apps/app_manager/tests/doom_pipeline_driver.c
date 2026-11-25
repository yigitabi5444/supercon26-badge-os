// One genuine signed join. Original engine objects reused; SDK/MMU/execution are doubled.
#define _DARWIN_C_SOURCE
#define main unused_transaction_vectors
#include "native_controller_driver.c"
#undef main
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#include "badge/badge.h"
#include "native_doom.h"
#include "native_elf.h"
#include "native_video.h"
#include "native_host_double.h"
#include "spi_flash_mmap.h"
#include "esp_elf.h"
#include "doomstat.h"
#include "g_game.h"
const app_t *const apps[]={NULL};const int napps=0;
void icon_base(int x,int y){(void)x;(void)y;}
void app_manager_ui(void){assert(0);}
static fixture_t *running;
static const char *wad_file;
static uintptr_t (*resolver)(const char *);
static unsigned loader_inits,loader_deinits,source_reads,phase;
static size_t temporary_live,temporary_peak,acquisition_preflight_peak,heap_live,heap_peak,wad_live;
static struct {void *pointer;size_t size;} blocks[256];
// The real clock advances during Doom's busy-wait wipes, independently of sleep.
int64_t esp_timer_get_time(void)
{uint64_t t=atomic_fetch_add(&nh_time_ms,1);if(!(t%50))nh_sleep();return (int64_t)t*1000;}
static void *temporary(size_t n)
{
    assert(n<=AM_DOOM_PAYLOAD_MAX);size_t *p=malloc(n+sizeof(size_t));assert(p);*p=n;
    temporary_live+=n;if(temporary_live>temporary_peak)temporary_peak=temporary_live;
    size_t pair=temporary_live+(running && running->source.doom_payload?AM_DOOM_PAYLOAD_MAX:0);
    if(pair>acquisition_preflight_peak)acquisition_preflight_peak=pair;return p+1;
}
static void temporary_free(void *v){size_t *p=(size_t *)v-1;temporary_live-=*p;free(p);}
void *heap_caps_realloc(void *old,size_t n,unsigned caps)
{
    assert(caps==3 && n);unsigned i=0;
    for(;i<256;i++)if(blocks[i].pointer==old)break;assert(i<256);
    void *p=realloc(old,n);if(!p)return NULL;
    if(old){nh_forget(old);if(blocks[i].size==BADGE_DOOM_WAD_BYTES)wad_live-=blocks[i].size;else heap_live-=blocks[i].size;}
    blocks[i].pointer=p;blocks[i].size=n;nh_memory(p,n,NH_PSRAM,true);
    if(n==BADGE_DOOM_WAD_BYTES)wad_live+=n;else heap_live+=n;
    if(heap_live>heap_peak)heap_peak=heap_live;return p;
}
void *heap_caps_malloc(size_t n,unsigned caps){return heap_caps_realloc(NULL,n,caps);}
void heap_caps_free(void *p)
{
    if(!p)return;unsigned i=0;for(;i<256;i++)if(blocks[i].pointer==p)break;assert(i<256);
    if(blocks[i].size==BADGE_DOOM_WAD_BYTES)wad_live-=blocks[i].size;else heap_live-=blocks[i].size;
    nh_forget(p);free(p);blocks[i].pointer=NULL;blocks[i].size=0;
}
esp_err_t spi_flash_mmap(uint32_t addr,size_t bytes,int kind,const void **out,spi_flash_mmap_handle_t *handle)
{
    assert(addr==0x650000 && bytes==0x1b0000 && kind==SPI_FLASH_MMAP_DATA && !temporary_live && !wad_live);
    int fd=open(wad_file,O_RDONLY);assert(fd>=0);void *p=mmap(NULL,bytes,PROT_READ,MAP_PRIVATE,fd,0);assert(p!=MAP_FAILED && !close(fd));
    *out=p;*handle=(uintptr_t)p;return ESP_OK;
}
void spi_flash_munmap(spi_flash_mmap_handle_t h){assert(!munmap((void *)h,0x1b0000));}
am_result_t am_psa_sha256_stream(am_reader_t r,void *c,uint32_t n,uint8_t out[32])
{return am_openssl_sha256_stream(r,c,n,out);}
am_result_t am_video_asset_read(const am_video_asset_t *a,uint32_t o,uint8_t *p,size_t n)
{(void)a;(void)o;(void)p;(void)n;assert(0);return AM_DISABLED;}
bool am_video_asset_live(const am_video_asset_t *a){(void)a;assert(0);return false;}
int nh_core_btn_held(void);
int btn_held(void)
{
    if(gamestate==GS_LEVEL && atomic_load(&nh_frames_done)>=20 && !phase){G_SaveGame(0,"LOCAL JOIN");phase=1;}
    if(phase && running) {
        char path[512];struct stat st;snprintf(path,sizeof path,"%s/doom/data/doomsav0.dsg",running->root);
        if(!stat(path,&st) && st.st_size>4096){phase=2;return NATIVE_PLUS|NATIVE_MINUS;}
    }
    return nh_core_btn_held();
}
void elf_set_symbol_resolver(uintptr_t (*fn)(const char *))
{assert(!resolver);resolver=fn;assert(resolver("badge_app_get_api")==((uintptr_t)badge_app_get_api));assert(!resolver("malloc"));}
void elf_reset_symbol_resolver(void){assert(resolver);resolver=NULL;}
int esp_elf_init(esp_elf_t *e){assert(resolver && !temporary_live);e->initialized=1;loader_inits++;return 0;}
int esp_elf_relocate(esp_elf_t *e,const uint8_t *p,size_t n){(void)e;(void)p;(void)n;assert(0);return -1;}
int esp_elf_relocate_source(esp_elf_t *e,const elf_source_t *s)
{
    assert(e->initialized && s && s->size(s->ctx)==328676 && !s->close && !temporary_live && !wad_live && !heap_live);
    uint8_t chunk[4096];for(size_t off=0;off<s->size(s->ctx);) {
        size_t n=s->size(s->ctx)-off;if(n>sizeof chunk)n=sizeof chunk;assert(!s->read(s->ctx,off,chunk,n));source_reads++;off+=n;
    }
    assert(s->read(s->ctx,s->size(s->ctx),chunk,1));return 0;
}
int doom_test_main(int,char **);
int esp_elf_request(esp_elf_t *e,int mode,int argc,char **argv)
{
    assert(e->initialized && !mode && !argc && !argv && running && !running->controller->execution_copy && running->controller->lease.owner);
    assert(!temporary_live && wad_live==BADGE_DOOM_WAD_BYTES && doom_test_main(0,NULL)==0 && phase==2);
    const badge_doom_api_t *a=(const void *)badge_app_get_api(2,sizeof *a);assert(a && !a->resize(a->base.context,NULL,UINT32_MAX));
    uint8_t bytes[4096];int32_t n=a->save_read(a->base.context,0,0,bytes,sizeof bytes);
    assert(n==4096 && !memcmp(bytes,"LOCAL JOIN",10));
    assert(am_store_uninstall(&running->controller->store)==AM_BUSY && !temporary_live && heap_live);
    return 0;
}
void esp_elf_deinit(esp_elf_t *e){assert(e->initialized && !wad_live && !heap_live);e->initialized=0;loader_deinits++;}
static void register_memory(void)
{
    size_t n=pthread_get_stacksize_np(pthread_self());uintptr_t high=(uintptr_t)pthread_get_stackaddr_np(pthread_self());
    nh_memory((void *)(high-n),n,NH_DRAM,true);
    const char *segments[]={"__DATA","__DATA","__DATA","__DATA_CONST","__TEXT","__TEXT"};
    const char *sections[]={"__bss","__common","__data","__const","__const","__cstring"};
    for(unsigned i=0;i<6;i++){unsigned long size=0;const uint8_t *p=getsectiondata(&_mh_execute_header,segments[i],sections[i],&size);
        if(p && size)nh_memory(p,size,i<3?NH_PSRAM:NH_DROM,i<3);}
    memset(fb,0,TOP*(W/8)); // Existing base.wipe clears the whole plane before the first frame.
}
static am_result_t open_doom(void *ctx,am_store_t *s,bool (*cancel)(void *),void *owner)
{
    am_result_t r=open_store(ctx,s,cancel,owner);s->doom_allocate=temporary;s->doom_free=temporary_free;
    ((fixture_t *)ctx)->host.vfs.quota=16*1024*1024;return r;
}
static am_result_t select_doom(void *ctx,am_store_t *s,const char *id,bool create,bool *ready)
{am_result_t r=select_app(ctx,s,id,create,ready);((fixture_t *)ctx)->host.vfs.quota=16*1024*1024;return r;}
static am_result_t fetch_doom(void *ctx,bool (*cancel)(void *),void *owner)
{
    fixture_t *f=ctx;assert(!strcmp(f->selected_id,"doom"));
    if(!f->source.doom_payload)f->source.doom_payload=malloc(AM_DOOM_PAYLOAD_MAX);assert(f->source.doom_payload);
    am_result_t r=fetch(ctx,cancel,owner);
    if(r==AM_OK)assert(am_native_elf_check(f->source.package.payload,"aliens","1.0.0",2)==AM_LIMIT);
    return r;
}
static void discard_doom(void *ctx)
{fixture_t *f=ctx;free(f->source.doom_payload);f->source.doom_payload=NULL;f->source.valid=false;f->source.package=(am_package_t){0};}
static am_result_t launch_doom(void *ctx,am_store_t *s,am_lease_t *l,const am_metadata_t *m,const char *path)
{
    fixture_t *f=ctx;assert(!f->deferred && !temporary_live && !f->source.doom_payload && !f->controller->execution_copy);
    nh_reset();register_memory();running=f;
    am_result_t r=am_native_doom_target_launch(s,l,m,path);
    assert(r==AM_OK && am_native_host_good_return() && !heap_live && !wad_live);
    for(unsigned i=0;i<TOP*(W/8);i++)assert(fb[i]==0);
    f->launched++;return r;
}
static am_result_t stop_host(void *ctx){(void)ctx;return app_manager_quiesce_snapshot();}
int main(int argc,char **argv)
{
    assert(argc==7);setbuf(stdout,NULL);wad_file=argv[6];fixture_t f={.root=argv[1],.key=argv[2],.folder=argv[3],.multiple=true};am_controller_t c;f.controller=&c;running=&f;
    uint8_t key[65];FILE *file=fopen(argv[5],"rb");assert(file && fread(key,1,65,file)==65 && !fclose(file));
    const am_source_config_t config={.public_key=key,.key_size=65,.manifest_url="https://packages.example/manifest.json",
        .signature_url="https://packages.example/manifest.sig",.payload_url="https://packages.example/module.elf"};
    f.online=!strcmp(argv[4],"install") || !strcmp(argv[4],"update");f.source.config=&config;
    f.source.bytes=calloc(1,sizeof *f.source.bytes);f.input=calloc(1,sizeof *f.input);assert(f.source.bytes && f.input);
    const am_platform_t p={.context=&f,.assigned=true,.open=open_doom,.close=close_store,.incoming=incoming,.fetch=fetch_doom,.discard=discard_doom,
        .allocate=allocate,.free=release,.home_mode=mode,.home_request=requested,.home_end=end,.launch_doom=launch_doom,.quiesce=stop_host,.select=select_doom};
    am_result_t r=am_controller_init(&c,p,(am_runtime_t){"esp32s3","6.0.2","2.1.0",2});unsigned id=2;assert(!strcmp(am_catalogue[id].id,"doom"));
    if(r==AM_OK)r=am_controller_action(&c,id,!strcmp(argv[4],"open")?AM_OPEN:!strcmp(argv[4],"update")?AM_UPDATE:!strcmp(argv[4],"uninstall")?AM_UNINSTALL:AM_INSTALL);
    bool installed=c.local[id].installed;
    assert(am_controller_shutdown(&c)==AM_OK && !c.execution_copy && !c.lease.owner && !f.deferred && !temporary_live && !heap_live && !wad_live);
    assert(f.allocated==f.freed && !resolver && loader_inits==loader_deinits && atomic_load(&nh_tasks_created)==atomic_load(&nh_tasks_deleted));
    printf("JOIN {\"result\":%d,\"installed\":%s,\"launched\":%u,\"temporary_peak\":%zu,\"acquisition_preflight_peak\":%zu,\"engine_heap_peak\":%zu,\"source_reads\":%u,\"frames\":%u}\n",
        r,installed?"true":"false",f.launched,temporary_peak,acquisition_preflight_peak,heap_peak,source_reads,atomic_load(&nh_frames_done));
    free(f.input);free(f.source.bytes);return 0;
}

size_t am_home_apps_inventory(const app_t **out,size_t capacity)
{(void)out;(void)capacity;assert(!"Home provider is outside this sealed app fixture");return 0;}
