// Actual Video host, signed asset/leases, portable player and pinned stb decoder, with OS/thread doubles.
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../badge_video_api.h"
#include "../native_video.h"
#include "../host_c/adapters.h"
#include "native_host_double.h"
#include "badge/badge.h"
#include "video_playback.h"

static bool fail_info;
static unsigned info_allocs,info_frees;
void *heap_caps_malloc(size_t n,unsigned caps)
{ (void)caps;if(fail_info)return NULL;if(n==(H-TOP)*(W/8))info_allocs++;return malloc(n); }
void *heap_caps_realloc(void *p,size_t n,unsigned caps){(void)caps;return realloc(p,n);}
void heap_caps_free(void *p){if(p)info_frees++;free(p);}
static am_result_t source_read(void *ctx,uint32_t off,uint8_t *out,size_t cap,size_t *got)
{ FILE *f=ctx;if(fseek(f,(long)off,SEEK_SET))return AM_IO;*got=fread(out,1,cap,f);return ferror(f)?AM_IO:AM_OK; }
static size_t small_file(const char *root,const char *name,uint8_t *out,size_t cap)
{
    char path[AM_VFS_PATH_MAX];int n=snprintf(path,sizeof path,"%s/%s",root,name);assert(n>0 && (size_t)n<sizeof path);
    FILE *f=fopen(path,"rb");assert(f);size_t got=fread(out,1,cap,f);assert(fgetc(f)==EOF && !ferror(f));fclose(f);return got;
}
typedef struct {
    const badge_video_api_t *api;unsigned polls,frames,busy,infos;bool want_info,oom;
} player_t;
static uint8_t compressed[BADGE_VIDEO_COMPRESSED_MAX],pixels[BADGE_VIDEO_PIXELS];
static void register_memory(void)
{
#if defined(__APPLE__)
    size_t n=pthread_get_stacksize_np(pthread_self());uintptr_t high=(uintptr_t)pthread_get_stackaddr_np(pthread_self());
    nh_memory((void *)(high-n),n,NH_DRAM,true);
#else
    uintptr_t here=(uintptr_t)&here;nh_memory((void *)(here-65536),131072,NH_DRAM,true);
#endif
    nh_memory(compressed,sizeof compressed,NH_PSRAM,true);nh_memory(pixels,sizeof pixels,NH_PSRAM,true);
}
static uint64_t body_hash(void)
{
    uint64_t hash=1469598103934665603ull;
    for(unsigned i=TOP*(W/8);i<PLANE;i++)hash=(hash^fb[i])*1099511628211ull;
    return hash;
}
static uint64_t now(void *ctx){player_t *p=ctx;return p->api->now_us(p->api->base.context);}
static void wait_time(void *ctx,uint64_t us){player_t *p=ctx;assert(us<=3000000);p->api->wait_us(p->api->base.context,(uint32_t)us);}
static void idle(void *ctx){player_t *p=ctx;p->api->base.frame_idle(p->api->base.context);}
static void wipe(void *ctx)
{
    player_t *p=ctx;p->api->base.wipe(p->api->base.context);
    memset(fb,0xa5,TOP*(W/8)); // Model the OS-owned bar after its scene change.
}
static bool read_asset(void *ctx,uint32_t offset,void *out,uint32_t n)
{
    player_t *p=ctx;uint8_t *dest=out;
    for(uint32_t done=0;done<n;) {
        uint32_t part=n-done;if(part>4096)part=4096;
        if(!p->api->asset_read(p->api->base.context,BADGE_VIDEO_CLIP,offset+done,dest+done,part))return false;
        done+=part;
    }
    return true;
}
static int32_t inflate(void *ctx,uint8_t *out,uint32_t n,const uint8_t *in,uint32_t size)
{ player_t *p=ctx;return p->api->inflate(p->api->base.context,out,n,in,size); }
static void blit(void *ctx,uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint8_t *bits)
{
    player_t *p=ctx;void *owner=p->api->base.context;
    if(!p->api->base.begin_frame(owner)){p->busy++;return;}
    assert(p->api->blit(owner,x,y,w,h,bits,BADGE_VIDEO_PIXELS));
    for(unsigned i=0;i<TOP*(W/8);i++)assert(fb[i]==0xa5);
    for(unsigned row=0;row<h;row++)assert(!memcmp(fb+(y+row)*(W/8)+x/8,bits+row*(w/8),w/8));
    p->api->base.submit_frame(owner);p->frames++;
}
static void info(void *ctx)
{
    player_t *p=ctx;idle(ctx);uint64_t before=body_hash();fail_info=p->oom;
    assert(p->api->info(p->api->base.context)==!p->oom);fail_info=false;
    assert(body_hash()==before);
    for(unsigned i=0;i<TOP*(W/8);i++)assert(fb[i]==0xa5);
    assert(!strcmp(nh_labels[0],"Info") && !nh_labels[1] && !strcmp(nh_labels[2],"Restart") && !strcmp(nh_labels[3],"Home"));
    p->infos++;
}
static enum video_key input(void *ctx)
{
    player_t *p=ctx;
    if(++p->polls==1 && p->want_info)nh_press(BTN_PLUS);
    else if(p->polls==4)nh_press(BTN_B);
    badge_native_input_t buttons;p->api->base.input(p->api->base.context,&buttons);
    if(buttons.pressed&NATIVE_B){assert(buttons.exit_requested);return VIDEO_HOME;}
    if(buttons.pressed&NATIVE_PLUS)return VIDEO_INFO;
    if(buttons.pressed&NATIVE_A)return VIDEO_RESTART;
    return VIDEO_NONE;
}
static int32_t failed_inflate(uint8_t *out,uint32_t n,const uint8_t *in,uint32_t size)
{ (void)out;(void)n;(void)in;(void)size;return -1; }
static void stopped(void)
{
    assert(am_native_host_stop()==AM_OK && !am_native_host_live());
    assert(atomic_load(&nh_sem_created)==atomic_load(&nh_sem_deleted));
    assert(atomic_load(&nh_tasks_created)==atomic_load(&nh_tasks_deleted));
}
static void playback(const am_native_video_t *binding,bool show_info,bool oom,bool fail)
{
    nh_reset();register_memory();am_native_video_t config=*binding;
    if(fail)config.inflate=failed_inflate;
    assert(am_native_host_start_video(&config)==AM_OK);
    const badge_video_api_t *api=(const badge_video_api_t *)badge_app_get_api(2,sizeof *api);assert(api);
    assert(api->base.struct_size==sizeof *api && api->asset_size(api->base.context,0)==819646);
    player_t player={.api=api,.want_info=show_info,.oom=oom};
    vid2_asset_t asset={&player,api->asset_size(api->base.context,0),read_asset};
    video_ops_t ops={&player,W,H,TOP,now,wait_time,wipe,idle,info,input,inflate,blit};
    assert(video_play_pass(&asset,&ops,compressed,sizeof compressed,pixels,sizeof pixels)==VIDEO_EXIT_HOME);
    assert(player.polls==4 && player.infos==(unsigned)show_info);
    assert(am_native_host_good_return()==!fail);
    if(!fail)assert(player.frames);
    stopped();assert(am_native_host_good_return()==!fail);
    assert(!api->asset_size(api->base.context,0));
}
static void pending_frame(const am_native_video_t *binding)
{
    nh_reset();register_memory();assert(am_native_host_start_video(binding)==AM_OK);
    const badge_video_api_t *api=(const badge_video_api_t *)badge_app_get_api(2,sizeof *api);assert(api);
    void *ctx=api->base.context;atomic_store(&nh_panel_blocked,true);
    assert(api->base.begin_frame(ctx));
    assert(api->blit(ctx,64,TOP,832,624,pixels,sizeof pixels));api->base.submit_frame(ctx);
    nh_await(&nh_frames_started,1);
    nh_press(BTN_B);badge_native_input_t buttons;api->base.input(ctx,&buttons);
    assert(buttons.exit_requested && am_native_host_good_return());
    assert(am_native_host_stop()==AM_BUSY && am_native_host_live());
    assert(am_video_asset_live(&binding->asset) && !atomic_load(&nh_tasks_deleted));
    atomic_store(&nh_panel_blocked,false);stopped();assert(am_native_host_good_return());
}
static void denied_blit(const am_native_video_t *binding)
{
    nh_reset();register_memory();assert(am_native_host_start_video(binding)==AM_OK);
    const badge_video_api_t *api=(const badge_video_api_t *)badge_app_get_api(2,sizeof *api);assert(api);
    uint64_t before=body_hash();
    assert(!api->blit(api->base.context,64,TOP,832,624,pixels,sizeof pixels)); // No frame lease.
    assert(body_hash()==before && !am_native_host_good_return());stopped();
}
int main(int argc,char **argv)
{
    assert(argc==5); // disposable root, existing public key, signed receipt dir, actual clip
    nh_reset();assert(am_native_host_start_video(NULL)==AM_DISABLED);
    am_host_t host;am_store_t store={.app_id="video",.runtime={"esp32s3","6.0.2","2.1.0",2}};
    store.work=calloc(1,sizeof *store.work);assert(store.work);
    assert(am_host_io(&host,argv[1],2*1024*1024,&store.io)==AM_OK);
    assert(am_openssl_crypto(argv[2],&store.crypto)==AM_OK);
    am_video_store_t video={&store,am_openssl_sha256_stream};am_lease_t lease={0};am_video_asset_t asset;
    assert(am_store_read_lease(&store,&lease)==AM_OK);
    assert(am_video_asset_open(&video,&lease,AM_VIDEO_CLIP_HASH,&asset)==AM_NOT_FOUND);
    am_native_video_t binding={.asset=asset,.inflate=am_native_video_inflate,.info=am_native_video_info};
    assert(am_native_host_start_video(&binding)==AM_DISABLED && !atomic_load(&nh_tasks_created));
    assert(am_store_release(&store,&lease)==AM_OK);
    uint8_t receipt[56],signature[128];size_t n=small_file(argv[3],"clip.receipt",receipt,sizeof receipt);
    size_t m=small_file(argv[3],"clip.sig",signature,sizeof signature);FILE *file=fopen(argv[4],"rb");assert(file);
    assert(am_video_asset_install(&video,(am_bytes_t){receipt,n},(am_bytes_t){signature,m},source_read,file)==AM_OK);fclose(file);
    assert(am_store_read_lease(&store,&lease)==AM_OK);
    assert(am_video_asset_open(&video,&lease,AM_VIDEO_CLIP_HASH,&binding.asset)==AM_OK);
    playback(&binding,false,false,false);playback(&binding,true,false,false);
    playback(&binding,true,true,false);playback(&binding,false,false,true);
    pending_frame(&binding);denied_blit(&binding);
    assert(info_allocs==info_frees);
    nh_reset();register_memory();assert(am_native_host_start_for("video")==AM_DISABLED);
    assert(am_native_host_start_for("aliens")==AM_OK);
    assert(!badge_app_get_api(2,sizeof(badge_video_api_t)));stopped(); // Other enrolled apps keep original getter size.
    nh_reset();register_memory();assert(am_native_host_start_video(&binding)==AM_OK);
    const badge_video_api_t *api=(const badge_video_api_t *)badge_app_get_api(2,sizeof *api);assert(api);
    assert(am_store_release(&store,&lease)==AM_OK);
    assert(!api->asset_size(api->base.context,0) && !am_native_host_good_return());stopped();
    am_openssl_close(&store.crypto);free(store.work);
    puts("PASS actual Video host + signed clip + original portable player + pinned stb: missing asset, bounded reads/inflate/packed blit, Info pause/exact restore and OOM, observed B clean return, failed inflate cleanup, old getter exclusion and stale lease revocation; OS/thread doubles, no ELF execution or device.");
    return 0;
}
