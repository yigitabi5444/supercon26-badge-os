// Production host/job/HTTPS/decode + exact module, SDK and device operations doubled.
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#endif
#include "esp_memory_utils.h"
#include "badge/badge.h"
#include "native_hackaday.h"
#include "native_video.h"
#include "badge_video_api.h"
#include "badge_codex_api.h"
#include "native_had_url.h"
#include "native_had_job.h"
#include "native_host_double.h"
#include "had_sdk_double.h"
int had_test_main(int,char **);
static const uint8_t *rss,*gzip_rss;static size_t rss_size,gzip_size;
static const uint8_t *png,*large_png;static size_t png_size,large_png_size;
am_result_t am_video_asset_read(const am_video_asset_t *a,uint32_t o,uint8_t *p,size_t n)
{(void)a;(void)o;(void)p;(void)n;assert(0);return AM_DISABLED;}
bool am_video_asset_live(const am_video_asset_t *a){(void)a;assert(0);return false;}
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
    memset(fb,0xa5,TOP*(W/8));
}
void had_test_prepare_module(void)
{
    nh_reset();had_sdk_reset();register_memory();had_sdk.body=rss;had_sdk.length=rss_size;
    had_sdk.image=png;had_sdk.image_length=png_size;
    nh_auto_chord_poll=30;
}
static const badge_hackaday_api_t *start_api(void)
{
    had_test_prepare_module();nh_auto_chord_poll=0;
    assert(am_native_host_start_hackaday(&am_native_hackaday_target)==AM_OK);
    const badge_hackaday_api_t *api=(const void *)badge_app_get_api(2,sizeof *api);assert(api);return api;
}
void had_test_assert_clean(void)
{
    assert(!am_native_host_live() && had_sdk.opens==had_sdk.cleans && !had_sdk.radio && had_sdk.radio_off==had_sdk.ranging);
    assert(atomic_load(&had_sdk.allocations)==atomic_load(&had_sdk.frees));
    assert(atomic_load(&nh_tasks_created)==atomic_load(&nh_tasks_deleted));
    assert(atomic_load(&nh_sem_created)==atomic_load(&nh_sem_deleted));
    for(unsigned i=0;i<TOP*(W/8);i++)assert(fb[i]==0xa5);
    for(unsigned i=0;i<4;i++)assert(!nh_labels[i]);
}
static void stop_api(void)
{assert(am_native_host_stop()==AM_OK);had_test_assert_clean();}
static int32_t completed(const badge_hackaday_api_t *api,badge_had_result_t *out)
{
    for(unsigned i=0;i<3000;i++) {
        int32_t status=api->poll(api->base.context,out);if(status!=BADGE_HAD_PENDING)return status;
        nh_sleep();
    }
    assert(0);return BADGE_HAD_FAILED;
}
static uint8_t *file_bytes(const char *name,size_t *size)
{
    FILE *f=fopen(name,"rb");assert(f && !fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);
    uint8_t *p=malloc((size_t)n);assert(p && fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);*size=(size_t)n;return p;
}
void had_test_fixtures(const char *plain,const char *compressed,const char *image,const char *large_image)
{rss=file_bytes(plain,&rss_size);gzip_rss=file_bytes(compressed,&gzip_size);
 png=file_bytes(image,&png_size);large_png=file_bytes(large_image,&large_png_size);}
void had_test_free_fixtures(void)
{free((void *)rss);free((void *)gzip_rss);free((void *)png);free((void *)large_png);}
static void url_policy(void)
{
    char host[254],out[400];const char *path;bool tls;
    assert(am_had_url("https://Hackaday.com/blog/feed/",true,host,&path,&tls) && tls && !strcmp(host,"hackaday.com"));
    assert(am_had_url("http://public.example/image.png?fit=10,10",false,host,&path,&tls) && !tls);
    const char *bad[]={"http://hackaday.com/blog/feed/","https://a@b/image","https://public.example:8080/image",
        "https://[::1]/image","https://public.example/image#part","https://public.example/\nimage"};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++)assert(!am_had_url(bad[i],true,host,&path,&tls));
    const uint32_t reserved[]={0,0x0a000001,0x64400001,0x7f000001,0xa9fe0001,0xac100001,0xc0000001,
        0xc0000201,0xc0586301,0xc0a80001,0xc6120001,0xc6336401,0xcb007101,0xe0000001,UINT32_MAX};
    for(unsigned i=0;i<sizeof reserved/sizeof *reserved;i++)assert(!am_had_public_ipv4(reserved[i]));
    assert(am_had_public_ipv4(0x08080808) && am_had_public_ipv4(0x01010101));
    assert(am_had_redirect("https://public.example/a/b?old", "?new",true,out) && !strcmp(out,"https://public.example/a/b?new"));
    assert(am_had_redirect("https://public.example/a/b", "next",true,out) && !strcmp(out,"https://public.example/a/next"));
    assert(am_had_redirect("https://public.example/a/b", "//cdn.example/image",true,out));
    assert(!am_had_redirect("https://hackaday.com/blog/feed/","http://public.example/",true,out));
}
static void transfers(void)
{
    for(unsigned mode=0;mode<9;mode++) {
        const badge_hackaday_api_t *api=start_api();void *ctx=api->base.context;badge_had_result_t result;
        if(mode==1){had_sdk.body=gzip_rss;had_sdk.length=gzip_size;had_sdk.encoding="gzip";}
        if(mode==2)had_sdk.private_dns=true;
        if(mode==3){had_sdk.status=302;had_sdk.location="http://public.example/feed";}
        if(mode==4)had_sdk.declared=BADGE_HAD_TRANSFER_MAX;
        if(mode==5)had_sdk.incomplete=true;
        if(mode==6)had_sdk.init_fail=true;
        if(mode==7){had_sdk.declared=0;had_sdk.chunk=7;}
        if(mode==8){had_sdk.status=302;had_sdk.location="https://127.0.0.1/feed";}
        assert(api->begin_feed(ctx)==1 && !api->begin_feed(ctx));
        int32_t status=completed(api,&result);
        assert(status==((mode<2 || mode==7)?BADGE_HAD_OK:BADGE_HAD_FAILED));
        if(status==BADGE_HAD_OK)assert(result.length==rss_size && !memcmp(result.data,rss,rss_size) && !result.data[result.length]);
        if(mode==2)assert(!had_sdk.connects);
        if(mode==3 || mode==4)assert(!had_sdk.reads);
        if(mode==8)assert(had_sdk.connects==1 && had_sdk.resolves==2);
        assert(api->end_job(ctx,1)==1 && api->release(ctx)==1);stop_api();
    }
}
static void image_decode(void)
{
    const badge_hackaday_api_t *api=start_api();void *ctx=api->base.context;badge_had_result_t result;
    assert(api->begin_feed(ctx) && completed(api,&result)==BADGE_HAD_OK && api->end_job(ctx,0));
    const char *urls[]={"http://media.example/image.png","https://media.example/image.png"};
    for(unsigned i=0;i<2;i++) {
        assert(api->begin_image(ctx,urls[i],16,16) && completed(api,&result)==BADGE_HAD_OK);
        assert(result.width==16 && result.height==16 && result.length==32);
        for(unsigned row=0;row<16;row++) {
            assert(result.data[row*2]==(row<8?255:0));assert(result.data[row*2+1]==(row<8?0:255));
        }
        assert(api->end_job(ctx,0));
    }
    had_sdk.image=large_png;had_sdk.image_length=large_png_size;
    assert(api->begin_image(ctx,urls[0],16,16) && completed(api,&result)==BADGE_HAD_FAILED);
    assert(api->end_job(ctx,1) && api->release(ctx));stop_api();
    am_had_job_t job={0};uint8_t bad[32]={0x1f,0x8b,8,8};memset(bad+10,'a',22);
    assert(!am_had_inflate(&job,bad,sizeof bad)); // Unterminated FNAME stays inside input.
    uint8_t *broken=malloc(gzip_size);assert(broken);memcpy(broken,gzip_rss,gzip_size);broken[gzip_size-8]^=1;
    assert(!am_had_inflate(&job,broken,(uint32_t)gzip_size));free(broken);
    assert(atomic_load(&had_sdk.allocations)==atomic_load(&had_sdk.frees));
}
static void scope_and_frames(void)
{
    const badge_hackaday_api_t *api=start_api();void *ctx=api->base.context;
    assert(!api->alloc(ctx,BADGE_HAD_FEED_MAX+1));void *large[4];
    for(unsigned i=0;i<4;i++){large[i]=api->alloc(ctx,BADGE_HAD_FEED_MAX);assert(large[i]);}
    assert(!api->alloc(ctx,1));for(unsigned i=0;i<4;i++)api->dealloc(ctx,large[i]);
    void *small[80];for(unsigned i=0;i<80;i++){small[i]=api->alloc(ctx,1);assert(small[i]);}
    assert(!api->alloc(ctx,1));for(unsigned i=0;i<80;i++)api->dealloc(ctx,small[i]);
    uint8_t *bits=api->alloc(ctx,2);assert(bits);bits[0]=0x81;bits[1]=0x80;
    char label[]="Prev";api->keys(ctx,label,NULL,"Open","Refresh");label[0]='X';
    assert(api->base.begin_frame(ctx));assert(!strcmp(nh_labels[0],"Prev"));api->base.clear(ctx);
    api->blit_mono(ctx,3,TOP,9,1,bits,2);
    assert(fb[TOP*(W/8)]==0x10 && fb[TOP*(W/8)+1]==0x30);
    api->base.submit_frame(ctx);api->keys(ctx,"Other",NULL,NULL,NULL);
    assert(!strcmp(nh_labels[0],"Prev"));api->base.frame_idle(ctx);
    assert(api->base.begin_frame(ctx));assert(!strcmp(nh_labels[0],"Other"));api->base.clear(ctx);
    api->blit_mono(ctx,8,TOP,9,1,bits,2);assert(fb[TOP*(W/8)+1]==0x81 && fb[TOP*(W/8)+2]==0x80);
    api->base.submit_frame(ctx);api->base.frame_idle(ctx);api->dealloc(ctx,bits);
    nh_press(BTN_B);assert(api->key(ctx,0)==BADGE_HAD_B && !am_native_host_good_return());
    atomic_store(&nh_held,NATIVE_A|NATIVE_B);assert(api->key(ctx,0)==BADGE_HAD_NO_KEY);
    atomic_fetch_add(&nh_time_ms,600);assert(api->key(ctx,0)==BADGE_HAD_EXIT);
    assert(api->release(ctx)==1 && am_native_host_good_return());stop_api();assert(am_native_host_good_return());
}
static void retained_cancel(void)
{
    const badge_hackaday_api_t *api=start_api();void *ctx=api->base.context;
    void *allocation=api->alloc(ctx,32);assert(allocation);atomic_store(&had_sdk.blocked,true);
    assert(api->begin_feed(ctx));
    for(unsigned i=0;i<3000 && !atomic_load(&had_sdk.reading);i++)nh_sleep();assert(atomic_load(&had_sdk.reading));
    assert(api->end_job(ctx,1)==0 && am_native_host_live() && esp_ptr_external_ram(allocation));
    assert(!api->begin_feed(ctx) && am_native_host_stop()==AM_BUSY && am_native_host_live());
    assert(esp_ptr_external_ram(allocation) && atomic_load(&nh_tasks_deleted)==0);
    atomic_store(&had_sdk.blocked,false);assert(am_native_host_stop()==AM_OK);had_test_assert_clean();
    assert(!esp_ptr_external_ram(allocation) && !am_native_host_good_return());
}
static void rejected_capabilities(void)
{
    const badge_hackaday_api_t *api=start_api();void *ctx=api->base.context;
    assert(!badge_app_get_api(2,sizeof(badge_video_api_t)) && !badge_app_get_api(2,sizeof(badge_codex_api_t)));
    assert(api->release(ctx));stop_api();
    api=start_api();ctx=api->base.context;uint8_t *allocation=api->alloc(ctx,8);assert(allocation);
    unsigned before=atomic_load(&had_sdk.frees);api->dealloc(ctx,allocation+1);
    assert(atomic_load(&had_sdk.frees)==before);api->dealloc(ctx,allocation);assert(api->release(ctx));stop_api();
    api=start_api();ctx=api->base.context;uint8_t unowned=0xff;
    assert(api->base.begin_frame(ctx));api->base.clear(ctx);api->blit_mono(ctx,0,TOP,8,1,&unowned,1);
    assert(!fb[TOP*(W/8)]);api->base.submit_frame(ctx);api->base.frame_idle(ctx);
    assert(api->release(ctx));stop_api();
    nh_reset();assert(am_native_host_start_for("hackaday")==AM_DISABLED);
    assert(am_native_host_start_for("aliens")==AM_OK && !badge_app_get_api(2,sizeof(badge_hackaday_api_t)));
    assert(am_native_host_stop()==AM_OK);
}
int main(int argc,char **argv)
{
    assert(argc==5);had_test_fixtures(argv[1],argv[2],argv[3],argv[4]);url_policy();transfers();image_decode();scope_and_frames();retained_cancel();rejected_capabilities();
    had_test_prepare_module();assert(am_native_host_start_hackaday(&am_native_hackaday_target)==AM_OK);
    assert(had_test_main(0,NULL)==0 && am_native_host_good_return());assert(am_native_host_stop()==AM_OK);had_test_assert_clean();
    had_test_free_fixtures();
    puts("PASS actual Hackaday host+job+HTTP/Clock+gzip+module: public pinned destination/TLS name, transfer bounds, copied labels/scoped heap/packed blit, original B and separate exit, retained busy cancel then exact cleanup; SDK calls doubled.");return 0;
}
