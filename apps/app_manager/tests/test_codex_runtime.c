// Real host + trusted target I/O + exact controller/parser/render; SDK calls doubled.
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#if defined(__APPLE__)
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#endif
#include "native_codex.h"
#include "badge_video_api.h"
#include "native_video.h"
#include "native_host_double.h"
#include "codex_sdk_double.h"
#include "badge/badge.h"
int cb_test_main(int,char **);
void cb_test_memory(void);
static const char valid[]=
    "CODEXBOARD\t1\nMETA\tabc\tLIVE\tAmerica/Los_Angeles\t2026-11-10 15:00:00 PST\n"
    "SOURCE\tOK\nQUOTA\tUNAVAILABLE\t\n"
    "PANE\t%1\tReview changes\tNEEDS_YOU\t00000000000000000000000000000001\n"
    "FOOTER\tquote\tReady when you are.\nEND\n";
am_result_t am_video_asset_read(const am_video_asset_t *a,uint32_t o,uint8_t *p,size_t n)
{(void)a;(void)o;(void)p;(void)n;assert(0);return AM_DISABLED;}
bool am_video_asset_live(const am_video_asset_t *a){(void)a;assert(0);return false;}
void cb_test_register(void)
{
#if defined(__APPLE__)
    size_t n=pthread_get_stacksize_np(pthread_self());uintptr_t high=(uintptr_t)pthread_get_stackaddr_np(pthread_self());
    nh_memory((void *)(high-n),n,NH_DRAM,true);
    unsigned long size=0;const uint8_t *p=getsectiondata(&_mh_execute_header,"__TEXT","__cstring",&size);
    if(p && size)nh_memory(p,size,NH_DROM,false);
    p=getsectiondata(&_mh_execute_header,"__TEXT","__asan_cstring",&size);
    if(p && size)nh_memory(p,size,NH_DROM,false);
    p=getsectiondata(&_mh_execute_header,"__TEXT","__const",&size);
    if(p && size)nh_memory(p,size,NH_DROM,false);
    p=getsectiondata(&_mh_execute_header,"__DATA","__const",&size);
    if(p && size)nh_memory(p,size,NH_DROM,false);
    p=getsectiondata(&_mh_execute_header,"__DATA_CONST","__const",&size);
    if(p && size)nh_memory(p,size,NH_DROM,false);
#else
    uintptr_t here=(uintptr_t)&here;nh_memory((void *)(here-65536),131072,NH_DRAM,true);
    extern char __executable_start,etext;nh_memory(&__executable_start,(size_t)(&etext-&__executable_start),NH_DROM,false);
#endif
    cb_test_memory();memset(fb,0xa5,TOP*(W/8));
}
void cb_test_prepare_module(bool lan)
{
    nh_reset();cb_sdk_reset();cb_test_register();nh_auto_b_poll=330;
    cb_sdk.body=valid;cb_sdk.usb_input=valid;
    cb_sdk.url=lan?"http://192.0.2.1:8765/feed":NULL;
}
void cb_test_assert_clean(void)
{
    assert(!am_native_host_live() && !cb_sdk.radio && !cb_sdk.clients && !cb_sdk.lit);
    assert(cb_sdk.opened==cb_sdk.cleaned && cb_sdk.off==cb_sdk.ranging);
    for(unsigned i=0;i<TOP*(W/8);i++)assert(fb[i]==0xa5);
    for(unsigned i=0;i<4;i++)assert(!nh_labels[i]);
}
static const badge_codex_api_t *start_api(void)
{
    nh_reset();cb_sdk_reset();cb_test_register();cb_sdk.url="http://192.0.2.1:8765/feed";cb_sdk.body=valid;
    assert(am_native_host_start_codex(&am_native_codex_target)==AM_OK);
    const badge_codex_api_t *api=(const void *)badge_app_get_api(2,sizeof *api);assert(api);return api;
}
static void stop_api(void)
{assert(am_native_host_stop()==AM_OK);cb_test_assert_clean();}
static void transport_limits(void)
{
    for(unsigned which=0;which<9;which++) {
        const badge_codex_api_t *api=start_api();void *ctx=api->base.context;
        assert(api->lan_join(ctx)==1);char out[8194];memset(out,0x5a,sizeof out);
        int32_t expected=(int32_t)strlen(valid);
        if(which==0)cb_sdk.complete_on_headers=true;
        if(which==1)cb_sdk.declared=0; // Unknown/chunked length, still fully drained.
        if(which==2){cb_sdk.status=302;expected=BADGE_CODEX_FETCH_HTTP;}
        if(which==3){cb_sdk.declared=8193;expected=BADGE_CODEX_FETCH_TOO_BIG;}
        if(which==4){cb_sdk.incomplete=true;expected=BADGE_CODEX_FETCH_INCOMPLETE;}
        if(which==5){cb_sdk.read_delay_ms=5001;expected=BADGE_CODEX_FETCH_INCOMPLETE;}
        if(which==6){cb_sdk.init_fail=true;expected=BADGE_CODEX_FETCH_INIT;}
        char huge[8194];memset(huge,'a',8193);huge[8193]=0;
        if(which==7){cb_sdk.body=huge;cb_sdk.declared=0;expected=BADGE_CODEX_FETCH_TOO_BIG;}
        if(which==8){cb_sdk.open_fail=true;expected=BADGE_CODEX_FETCH_OFFLINE;}
        assert(api->lan_fetch(ctx,out+1,8192)==expected && out[0]==0x5a && out[8193]==0x5a);
        if(which<2)assert(cb_sdk.reads && !memcmp(out+1,valid,strlen(valid)));
        if(which==2 || which==3)assert(!cb_sdk.reads);
        api->release(ctx);assert(!am_native_host_good_return());stop_api();
    }
}
static void guards(void)
{
    nh_reset();assert(am_native_host_start_for("codex_board")==AM_DISABLED);
    assert(am_native_host_start_for("aliens")==AM_OK);
    assert(!badge_app_get_api(2,sizeof(badge_codex_api_t)));assert(am_native_host_stop()==AM_OK);
    const badge_codex_api_t *api=start_api();assert(!badge_app_get_api(2,sizeof(badge_video_api_t)));stop_api();
    api=start_api();char out[8192];assert(api->lan_fetch(api->base.context,out,8191)==BADGE_CODEX_FETCH_INCOMPLETE);
    assert(!cb_sdk.clients && !am_native_host_good_return());stop_api();
    api=start_api();assert(!api->usb_ack(api->base.context,"not-a-revision"));assert(!cb_sdk.acks);stop_api();
    api=start_api();api->release(api->base.context);assert(api->mode(api->base.context)==BADGE_CODEX_INVALID);stop_api();
    api=start_api();char label[]="Next page";
    assert(api->base.begin_frame(api->base.context));
    api->keys(api->base.context,label,NULL,"Redraw","Home");label[0]='X';
    assert(!strcmp(nh_labels[0],"Next page")); // Copied label survives caller changes.
    api->base.submit_frame(api->base.context);api->base.frame_idle(api->base.context);
    nh_press(BTN_B);assert(api->button(api->base.context,100)==BADGE_CODEX_B);
    assert(!am_native_host_good_return()); // Forced cleanup cannot invent explicit release evidence.
    stop_api();assert(!am_native_host_good_return());
    api=start_api();assert(!api->seen_load(api->base.context,(char *)(uintptr_t)1,1057));stop_api();
    api=start_api();assert(!api->usb_read(api->base.context,out,257));stop_api();
    nh_reset();cb_sdk_reset();nh_fail_task=1;
    assert(am_native_host_start_codex(&am_native_codex_target)==AM_FULL && !am_native_host_live());
}
#ifndef CB_NO_MAIN
int main(void)
{
    transport_limits();guards();
    for(unsigned run=0;run<3;run++) {
        cb_test_prepare_module(run==1);
        if(run==2)cb_sdk.url="https://invalid.example/feed";
        assert(am_native_host_start_codex(&am_native_codex_target)==AM_OK);
        assert(cb_test_main(0,NULL)==0 && am_native_host_good_return());
        assert(am_native_host_stop()==AM_OK && am_native_host_good_return());cb_test_assert_clean();
        if(run==0)assert(cb_sdk.installed && cb_sdk.readies==1 && cb_sdk.acks==1 && cb_sdk.saves==1 && cb_sdk.led_on>1 && !cb_sdk.joins);
        if(run==1)assert(cb_sdk.joins==1 && cb_sdk.opened>1 && cb_sdk.saves==1 && cb_sdk.led_on>1 && !cb_sdk.readies);
        if(run==2)assert(!cb_sdk.joins && !cb_sdk.readies && !cb_sdk.saves);
    }
    puts("PASS actual Codex Board entry/controller/parser/render + host/target: USB resident driver/READY/split feed/ACK, LAN bounded HTTP/errors, seen-before-flash, stale/ordinary B cleanup, selected capability and buffer guards; SDK calls doubled.");
    return 0;
}
#endif
