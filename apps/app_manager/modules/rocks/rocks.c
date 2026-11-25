// Rocks: a ship in open space below the OS top bar. Hold MENU + / MENU - to turn left / right, hold A to thrust,
// B fires (press, or hold for a steady stream). The AS5600 knob add-on, when turned, sets the heading directly.
// Rocks drift and wrap around the edges; a hit rock splits in two smaller ones, the smallest vanish. A cleared
// field shakes the ghosts off the panel and brings more rocks. Three ships; game over shows the score for 3 s,
// then a new game starts by itself.
// Like Snake: the game ticks on its own clock (25/s), a second task hands the latest state to the panel whenever
// it takes a frame (~3.5-4/s).
// Everything is drawn as 3 px outlines that wrap around the play field, so a frame changes few pixels.
// Optional native Asteroids port; builtin apps/rocks unchanged. GPL-2.0, see root LICENSE.
#include <stddef.h>
#include "badge_native_api.h"
#define ROCKS_APP_VERSION "1.0.0"
BADGE_NATIVE_INFO("rocks",ROCKS_APP_VERSION);
static const badge_native_api_t *api;
#ifdef __XTENSA__
// GCC lowers the original rock aggregate initializer to memset even without builtins.
void *memset(void *out,int value,size_t n)
{
    volatile unsigned char *p=out;while(n--)*p++=(unsigned char)value;return out;
}
#endif
static uint32_t module_random(void) { return api->random(api->context); }
#include "model.h"
// Newlib libm's errno stays inside this single-task module; no host libc import.
static int math_errno;
int *__errno(void) { return &math_errno; }
static void fill(void *out,unsigned n)
{
    volatile unsigned char *p=out;while(n--)*p++=0;
}
static void module_clear(void) { api->clear(api->context); }
static void module_box(int x,int y,int w,int h,int ink) { api->box(api->context,x,y,w,h,(uint32_t)ink); }
static void module_outline(int x,int y,int w,int h,int t) { api->outline(api->context,x,y,w,h,t,1); }
static void module_text(int x,int y,int scale,const char *s,int ink) { api->text(api->context,x,y,scale,s,(uint32_t)ink); }
#include "render.h"
static bool input(badge_native_input_t *in)
{
    api->input(api->context,in);return in->exit_requested!=0;
}
static bool render_available(void)
{
    if(api->begin_frame(api->context)<=0)return false;
    rocks_draw();api->submit_frame(api->context);return true;
}
static bool pause_game(uint32_t duration)
{
    uint32_t start=api->now_ms(api->context);
    for(;;) {
        uint32_t elapsed=api->now_ms(api->context)-start;if(elapsed>=duration)return true;
        badge_native_input_t in;if(input(&in))return false;
        uint32_t left=duration-elapsed;api->wait_ms(api->context,left<TICK_MS?left:TICK_MS);
    }
}
static int run_game(void)
{
    badge_native_input_t in;api->wipe(api->context);if(input(&in))return 0;
    ref=in.knob;
    for(;;) {
        fill(&g,sizeof g);g.lives=3;spawn();int level=1;cool=0;wave(level);
        if(input(&in))return 0; // discard queued taps before the game, as btn_flush did
        uint32_t due=api->now_ms(api->context);
        for(;;) {
            due+=TICK_MS;int32_t delay=(int32_t)(due-api->now_ms(api->context));
            if(delay>0)api->wait_ms(api->context,(uint32_t)delay);
            if(input(&in))return 0;
            bool clear=rocks_tick(in.held,(in.pressed&NATIVE_B)!=0,in.knob);
            bool submitted=render_available();
            if(g.over) {
                while(!submitted) {
                    if(input(&in))return 0;
                    api->wait_ms(api->context,TICK_MS);submitted=render_available();
                }
                api->frame_idle(api->context);
                if(!pause_game(3000))return 0;
                api->wipe(api->context);break;
            }
            if(clear) {
                api->wipe(api->context);wave(++level);fill(g.sh,sizeof g.sh);g.shield=SHIELD;
                due=api->now_ms(api->context);
            }
        }
    }
}
int rocks_run(void)
{
    api=badge_app_get_api(BADGE_NATIVE_ABI,sizeof *api);
    if(!api)return -2;
    if(api->abi_version!=BADGE_NATIVE_ABI||api->struct_size<sizeof *api)return -1;
    if(!api->context||api->width!=W||api->height!=H||api->top!=TOP||
       !api->now_ms||!api->wait_ms||!api->input||!api->random||!api->begin_frame||!api->submit_frame||
       !api->clear||!api->box||!api->outline||!api->text||!api->frame_idle||!api->wipe)return -2;
    int result=run_game();api->frame_idle(api->context);api=NULL;return result;
}
int main(int argc,char **argv) { (void)argc;(void)argv;return rocks_run(); }
