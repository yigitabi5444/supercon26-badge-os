// Focused proof against actual unchanged apps/rocks/rocks.c, not target acceptance.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "badge_native_api.h"
#undef BADGE_NATIVE_INFO
#define BADGE_NATIVE_INFO(id_,v_) static const badge_native_info_t badge_native_info __attribute__((used,aligned(4)))={BADGE_NATIVE_INFO_MAGIC,BADGE_NATIVE_ABI,id_,v_}
#define main rocks_fixture_main
#include "rocks.c"
#undef main
#include "badge/font8x8_basic.h"
#define PLANE (W*H/8)
static uint8_t fb[PLANE],expected[PLANE];
static uint32_t rng_main,rng_ref;
static uint32_t next(uint32_t *s) { return *s=*s*1664525u+1013904223u; }
static uint32_t esp_random(void) { return next(&rng_ref); }
static void box(int x,int y,int w,int h,bool ink)
{
    int left=x<0?0:x,top=y<0?0:y,right=x+w>W?W:x+w,bottom=y+h>H?H:y+h;
    for(int Y=top;Y<bottom;Y++)for(int X=left;X<right;X++) {
        uint8_t mask=(uint8_t)(128>>(X&7));
        if(ink)fb[Y*(W/8)+X/8]|=mask;else fb[Y*(W/8)+X/8]&=(uint8_t)~mask;
    }
}
static void outline(int x,int y,int w,int h,int t)
{ box(x,y,w,t,true);box(x,y+h-t,w,t,true);box(x,y,t,h,true);box(x+w-t,y,t,h,true); }
static void text(int x,int y,int scale,const char *s,bool ink)
{
    for(;*s;s++,x+=8*scale)for(int r=0;r<8;r++)for(int c=0;c<8;c++)
        if(font8x8_basic[(unsigned char)*s&127][r]>>c&1)box(x+c*scale,y+r*scale,scale,scale,ink);
}
#define xSemaphoreTake(...) ((void)0)
#define xSemaphoreGive(...) ((void)0)
#include "reference.h"
#undef xSemaphoreTake
#undef xSemaphoreGive
static struct {
    uint32_t now,busy_until,held;int calls,stop_at,frames,busy,idles,wipes;
    bool lease,inflight;
} host;
static badge_native_api_t table;
const badge_native_api_t *badge_app_get_api(uint32_t abi,uint32_t size)
{ assert(abi==BADGE_NATIVE_ABI&&size==sizeof table);return &table; }
static uint32_t now_ms(void *c) { (void)c;return host.now; }
static void wait_ms(void *c,uint32_t ms) { (void)c;assert(!host.lease&&ms<=40);host.now+=ms; }
static void get_input(void *c,badge_native_input_t *in)
{
    (void)c;host.calls++;
    *in=(badge_native_input_t){host.held,0,-1,host.stop_at&&host.calls>=host.stop_at};
}
static uint32_t random32(void *c) { (void)c;return next(&rng_main); }
static int32_t begin(void *c)
{
    (void)c;assert(!host.lease);
    if(host.inflight&&(int32_t)(host.now-host.busy_until)<0){host.busy++;return 0;}
    host.inflight=false;host.lease=true;return 1;
}
static void submit(void *c)
{ (void)c;assert(host.lease);host.lease=false;host.inflight=true;host.busy_until=host.now+240;host.frames++; }
static void clear(void *c) { (void)c;assert(host.lease);memset(fb+TOP*W/8,0,PLANE-TOP*W/8); }
static void rect(void *c,int32_t x,int32_t y,int32_t w,int32_t h,uint32_t ink)
{ (void)c;assert(host.lease&&x>=0&&y>=TOP&&x+w<=W&&y+h<=H);box(x,y,w,h,ink!=0); }
static void border(void *c,int32_t x,int32_t y,int32_t w,int32_t h,int32_t t,uint32_t ink)
{ (void)c;assert(host.lease&&ink);outline(x,y,w,h,t); }
static void label(void *c,int32_t x,int32_t y,int32_t scale,const char *s,uint32_t ink)
{ (void)c;assert(host.lease);text(x,y,scale,s,ink!=0); }
static void idle(void *c)
{
    (void)c;assert(!host.lease);
    if(host.inflight&&(int32_t)(host.now-host.busy_until)<0)host.now=host.busy_until;
    host.inflight=false;host.idles++;
}
static void wipe(void *c) { idle(c);host.wipes++; }
static void reset(void)
{
    memset(&host,0,sizeof host);rng_main=rng_ref=17;
    table=(badge_native_api_t){BADGE_NATIVE_ABI,sizeof table,&host,W,H,TOP,now_ms,wait_ms,get_input,random32,begin,submit,clear,rect,border,label,idle,wipe};
    api=&table;fill(&g,sizeof g);ref=baseline_ref=-1;cool=baseline_cool=0;
    g.lives=3;spawn();wave(1);
    memset(&baseline_g,0,sizeof baseline_g);baseline_g.lives=3;baseline_spawn();baseline_wave(1);
    assert(sizeof g==2256&&sizeof baseline_g==sizeof g);
    assert(!memcmp(&g,&baseline_g,sizeof g)&&rng_main==rng_ref);
}
static void mirror(void) { memcpy(&baseline_g,&g,sizeof g);baseline_ref=ref;baseline_cool=cool;rng_ref=rng_main; }
static int ticks,frames;
static bool check_tick(uint32_t held,bool fire,int knob)
{
    bool a=rocks_tick(held,fire,knob),b=baseline_tick(held,fire,knob);
    assert(a==b&&!memcmp(&g,&baseline_g,sizeof g));
    assert(ref==baseline_ref&&cool==baseline_cool&&rng_main==rng_ref);ticks++;return a;
}
static void check_render(void)
{
    mirror();memset(fb,0,sizeof fb);baseline_draw();memcpy(expected,fb,PLANE);
    host.lease=true;rocks_draw();host.lease=false;
    assert(!memcmp(expected+TOP*W/8,fb+TOP*W/8,PLANE-TOP*W/8));frames++;
}
int main(int argc,char **argv)
{
    assert(argc==2);reset();check_render();
    FILE *f=fopen(argv[1],"wb");assert(f);assert(fprintf(f,"P4\n%d %d\n",W,H)>0);
    assert(fwrite(fb,1,PLANE,f)==PLANE&&!fclose(f));
    for(int t=0;t<40;t++)check_tick(NATIVE_A|NATIVE_B,false,-1);
    assert(g.thrust&&g.vy<0);check_render();
    for(int t=0;t<180;t++)check_tick((t%3?NATIVE_PLUS:NATIVE_MINUS)|(t%5?NATIVE_A:0)|(t%4?NATIVE_B:0),t%7==0,-1);
    check_render();
    reset();g.x=W-1;g.y=PH-1;g.a=21;g.vx=350;g.vy=200;mirror();
    check_tick(NATIVE_A,false,-1);check_render();
    for(int t=0;t<32;t++)check_tick(NATIVE_A,t%6==0,(4090+t*31)%4096);
    check_render();
    reset();fill(g.rk,sizeof g.rk);fill(g.sh,sizeof g.sh);
    g.rk[0]=(rock_t){.x=300,.y=200,.size=3};g.sh[0]=(shot_t){.x=300,.y=200,.life=5};mirror();
    check_tick(0,false,-1);assert(g.score==20&&g.rk[0].size==2&&g.rk[1].size==2);check_render();
    reset();fill(g.rk,sizeof g.rk);g.shield=0;g.lives=1;
    g.rk[0]=(rock_t){.x=g.x,.y=g.y,.size=1};mirror();
    check_tick(0,false,-1);assert(g.over&&g.lives==0&&g.gone==RESPAWN);check_render();
    g.score=2147483647;check_render();
    reset();g.gone=10;check_render();
    reset();fill(g.rk,sizeof g.rk);mirror();assert(check_tick(0,false,-1));
    wave(2);baseline_wave(2);assert(!memcmp(&g,&baseline_g,sizeof g)&&rng_main==rng_ref);check_render();
    reset();host.held=NATIVE_A|NATIVE_B;host.stop_at=32;
    assert(!rocks_run());assert(g.t==29&&g.thrust&&g.vy<0&&host.now>=1160);
    assert(host.frames>=4&&host.busy>0&&!host.lease&&!host.inflight&&!api);
    // stop_at is synthetic harness teardown, not an Asteroids normal-exit gesture.
    printf("PASS:%d bit-identical model ticks + wave/reset/split/death/knob/wrap against actual original; state2256B\n",ticks);
    printf("PASS:%d pixel-identical original frames including wrapped outlines/flame/shield/score/gameover\n",frames);
    puts("PASS:40ms model while renderer busy; sustained thrust+fire beyond600ms; synthetic harness teardown only. No target/Home exit proof.");
}
