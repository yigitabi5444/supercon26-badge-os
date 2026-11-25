// Actual module + unchanged builtin drawing, with a nonblocking frame-broker fixture.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "badge_native_api.h"
#undef BADGE_NATIVE_INFO
#define BADGE_NATIVE_INFO(id_,version_) static const badge_native_info_t badge_native_info __attribute__((used,aligned(4)))={BADGE_NATIVE_INFO_MAGIC,BADGE_NATIVE_ABI,id_,version_}
#define main aliens_fixture_main
#include "aliens.c"
#undef main
#include "badge/font8x8_basic.h"
#define PLANE (W*H/8)
static uint8_t fb[PLANE],expected[PLANE];
typedef struct {
    uint32_t now,busy_until,rng,held,pressed;
    int inputs,stop_at,force_over,knob_case,frames,busy,waits,idles,wipes;
    uint32_t over_idle,second_wipe;
    bool lease,inflight,knob_after_wipe;
} host_t;
static host_t host;
static bool unavailable;
static badge_native_api_t table;
static void box(int x,int y,int w,int h,bool ink) {
    int left=x<0?0:x,top=y<0?0:y,right=x+w>W?W:x+w,bottom=y+h>H?H:y+h;
    for(int Y=top;Y<bottom;Y++)for(int X=left;X<right;X++) {
        uint8_t mask=(uint8_t)(128>>(X&7));if(ink)fb[Y*(W/8)+X/8]|=mask;else fb[Y*(W/8)+X/8]&=(uint8_t)~mask;
    }
}
static void outline(int x,int y,int w,int h,int t) { box(x,y,w,t,true);box(x,y+h-t,w,t,true);box(x,y,t,h,true);box(x+w-t,y,t,h,true); }
static void text(int x,int y,int scale,const char*s,bool ink) {
    for(;*s;s++,x+=8*scale)for(int r=0;r<8;r++)for(int c=0;c<8;c++)
        if(font8x8_basic[(unsigned char)*s&127][r]>>c&1)box(x+c*scale,y+r*scale,scale,scale,ink);
}
#define xSemaphoreTake(...) ((void)0)
#define xSemaphoreGive(...) ((void)0)
#include "reference_render.h"
#undef xSemaphoreTake
#undef xSemaphoreGive
static uint32_t now_ms(void*c) { return ((host_t*)c)->now; }
static void wait_ms(void*c,uint32_t ms) { host_t*h=c;assert(!h->lease&&ms<=1000);h->now+=ms;h->waits++; }
static void input(void*c,badge_native_input_t*i) {
    host_t*h=c;h->inputs++;i->held=h->held;i->pressed=h->pressed;i->knob=-1;i->exit_requested=h->stop_at&&h->inputs>=h->stop_at;
    if(h->knob_after_wipe)i->knob=h->wipes?1000:0;
    if(h->knob_case)i->knob=h->inputs<3?0:h->inputs==3?4090:4080;
    if(h->force_over&&h->inputs==h->force_over){game.dead=1;game.lives=0;}
}
static uint32_t random32(void*c) { host_t*h=c;return h->rng=h->rng*1664525u+1013904223u; }
static int32_t begin(void*c) {
    host_t*h=c;assert(!h->lease);
    if(h->inflight&&(int32_t)(h->now-h->busy_until)<0){h->busy++;return 0;}
    h->inflight=false;h->lease=true;return 1;
}
static void submit(void*c) { host_t*h=c;assert(h->lease);h->lease=false;h->inflight=true;h->busy_until=h->now+240;h->frames++; }
static void clear(void*c) { assert(((host_t*)c)->lease);memset(fb+TOP*W/8,0,PLANE-TOP*W/8); }
static void rect(void*c,int32_t x,int32_t y,int32_t w,int32_t h,uint32_t ink) { assert(((host_t*)c)->lease);assert(y>=TOP);box(x,y,w,h,ink!=0); }
static void border(void*c,int32_t x,int32_t y,int32_t w,int32_t h,int32_t t,uint32_t ink) { assert(((host_t*)c)->lease&&ink);outline(x,y,w,h,t); }
static void label(void*c,int32_t x,int32_t y,int32_t scale,const char*s,uint32_t ink) { assert(((host_t*)c)->lease&&strlen(s)<32);text(x,y,scale,s,ink!=0); }
static void idle(void*c) {
    host_t*h=c;assert(!h->lease);if(h->inflight&&(int32_t)(h->now-h->busy_until)<0)h->now=h->busy_until;h->inflight=false;h->idles++;
    if(game.over)h->over_idle=h->now;
}
static void wipe(void*c) {
    host_t*h=c;assert(!h->lease);if(h->inflight&&(int32_t)(h->now-h->busy_until)<0)h->now=h->busy_until;h->inflight=false;
    if(++h->wipes==2){h->second_wipe=h->now;assert((uint32_t)(h->second_wipe-h->over_idle)>=3000);}
    memset(fb+TOP*W/8,0,PLANE-TOP*W/8);
}
static void reset(void) {
    memset(&host,0,sizeof host);host.rng=17;host.stop_at=5;unavailable=false;
    table=(badge_native_api_t){BADGE_NATIVE_ABI,sizeof table,&host,W,H,TOP,now_ms,wait_ms,input,random32,begin,submit,clear,rect,border,label,idle,wipe};
    aliens_fill(&game,0,sizeof game);api=NULL;
}
const badge_native_api_t *badge_app_get_api(uint32_t abi,uint32_t size) { assert(abi==BADGE_NATIVE_ABI&&size==sizeof table);return unavailable?NULL:&table; }
static void pixels(void) {
    reset();api=&table;
    for(int n=0;n<16;n++) {
        memset(&baseline_g,0,sizeof baseline_g);baseline_g.lives=3;baseline_g.wave=1+n;baseline_g.px=(W-GW)/2;
        baseline_g.fx=40+n*4;baseline_g.fy=TOP+56;baseline_g.frame=n&1;baseline_g.sy=n%2?PY-50:-1;baseline_g.sx=450;
        baseline_g.score=n==15?2147483647:n*123;baseline_g.over=n%3==0;baseline_g.dead=n%4==0;
        for(int i=0;i<NA;i++){baseline_g.alive[i]=(i+n)%4!=0;baseline_g.boom[i]=(i+n)%4==0;}
        for(int k=0;k<NB;k++){baseline_g.bx[k]=100+k*220;baseline_g.by[k]=TOP+190+k*70;}
        memset(fb,0,sizeof fb);baseline_draw();text(W-64,H-16,1,"v" ALIENS_APP_VERSION,true);memcpy(expected,fb,PLANE);
        memcpy(&game,&baseline_g,sizeof game);host.lease=true;aliens_draw(&game);host.lease=false;
        assert(!memcmp(expected+TOP*W/8,fb+TOP*W/8,PLANE-TOP*W/8));
    }
    puts("PASS:16 native body/HUD/sprite frames vs original plus version footer, including shots, bursts, game over and maximum score");
}
int main(void) {
    pixels();reset();unavailable=true;assert(aliens_run()==-2&&!host.wipes);
    reset();table.abi_version=1;assert(aliens_run()==-1&&!host.wipes);
    reset();table.struct_size--;assert(aliens_run()==-1&&!host.wipes);
    reset();table.top++;assert(aliens_run()==-2&&!host.wipes);
    reset();table.random=NULL;assert(aliens_run()==-2&&!host.wipes);
    reset();host.held=NATIVE_A|NATIVE_PLUS;assert(!aliens_run());assert(game.px==(W-GW)/2-2*GUN_V&&game.sy==PY-24-2*SHOT_V);
    assert(host.busy==2&&host.frames==1&&host.waits==3&&!host.lease&&!host.inflight&&!api);
    reset();host.stop_at=4;host.pressed=NATIVE_MINUS;assert(!aliens_run());assert(game.px==(W-GW)/2&&game.sy==PY-24-SHOT_V);
    reset();host.knob_after_wipe=true;assert(!aliens_run());assert(game.px==(W-GW)/2);
    reset();host.knob_case=1;assert(!aliens_run());assert(game.px==(W-GW)/2-3);
    reset();host.force_over=6;host.stop_at=90;assert(!aliens_run());assert(host.wipes==2&&host.second_wipe-host.over_idle>=3000);
    reset();host.now=UINT32_MAX-90;host.held=NATIVE_B;assert(!aliens_run());assert(game.px==(W-GW)/2+2*GUN_V);
    puts("PASS: ABI/dimension/null validation, physical held+tap controls/knob,50ms model while frame busy, normal exit/no lease, visible game-over/3s/wipe/restart and clock wrap");
}
