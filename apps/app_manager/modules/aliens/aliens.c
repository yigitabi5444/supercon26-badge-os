// Optional port of Badge OS SpaceInv (apps/aliens/aliens.c, aab00d5); GPL-2.0.
// No builtin, SDK, task, storage or panel-driver dependency. Host owns the frame broker.
#include <stddef.h>
#include "badge_native_api.h"
#include "game.h"
#define ALIENS_APP_VERSION "1.0.2"
BADGE_NATIVE_INFO("aliens", ALIENS_APP_VERSION);
static const badge_native_api_t *api;
static aliens_game_t game;
#include "render.h"

static void module_clear(void) { api->clear(api->context); }
static void module_box(int x,int y,int w,int h,int ink) { api->box(api->context,x,y,w,h,(uint32_t)ink); }
static void module_outline(int x,int y,int w,int h,int t,int ink) { api->outline(api->context,x,y,w,h,t,(uint32_t)ink); }
static void module_text(int x,int y,int scale,const char *s,int ink) { api->text(api->context,x,y,scale,s,(uint32_t)ink); }
static uint32_t module_random(void *context) { return api->random(context); }

static bool exit_requested(badge_native_input_t *in)
{
    api->input(api->context,in);
    return in->exit_requested != 0;
}

static bool render_available(void)
{
    if (api->begin_frame(api->context) <= 0) return false;
    aliens_draw(&game);
    api->submit_frame(api->context);
    return true;
}

static bool pause_game(uint32_t duration)
{
    uint32_t start=api->now_ms(api->context);
    for (;;) {
        uint32_t elapsed=api->now_ms(api->context)-start;
        if (elapsed>=duration) break;
        badge_native_input_t in;
        if (exit_requested(&in)) return false;
        uint32_t left=duration-elapsed;
        api->wait_ms(api->context,left<TICK_MS?left:TICK_MS);
    }
    return true;
}

static int run_game(void)
{
    badge_native_input_t in;
    api->wipe(api->context); // Home cleans the scene before the builtin first samples its knob
    if (exit_requested(&in)) return 0;
    int ref=in.knob;
    for (;;) {
        aliens_fill(&game,0,sizeof game);
        game.lives=3;game.px=(W-GW)/2;aliens_new_wave(&game);
        while (!render_available()) {
            if (exit_requested(&in)) return 0;
            api->wait_ms(api->context,TICK_MS);
        }
        if (exit_requested(&in)) return 0; // discard pre-game queued taps, retaining held state
        uint32_t due=api->now_ms(api->context);
        bool over_submitted=false;
        while (!game.over) {
            due+=TICK_MS;
            int32_t delay=(int32_t)(due-api->now_ms(api->context));
            if (delay>0) api->wait_ms(api->context,(uint32_t)delay);
            if (exit_requested(&in)) return 0;
            int fire=(in.held|in.pressed)&(NATIVE_PLUS|NATIVE_MINUS);
            int move=((in.held&NATIVE_B?1:0)-(in.held&NATIVE_A?1:0))*GUN_V;
            int a=in.knob,d=((a-ref)%4096+6144)%4096-2048;
            if (a<0||ref<0) ref=a;
            else if (d>=8||d<=-8) move+=KNOB_DIR*d*(W-GW)/4096,ref=a;
            aliens_tick(&game,fire,move,module_random,api->context);
            bool shown=render_available();
            if (game.over) over_submitted=shown;
        }
        while (!over_submitted) {
            if (exit_requested(&in)) return 0;
            over_submitted=render_available();
            if (!over_submitted) api->wait_ms(api->context,TICK_MS);
        }
        api->frame_idle(api->context); // game-over frame complete and panel closed before the 3s pause
        if (!pause_game(3000)) return 0;
        api->wipe(api->context);
    }
}

int aliens_run(void)
{
    api=badge_app_get_api(BADGE_NATIVE_ABI,sizeof(badge_native_api_t));
    if (!api) return -2;
    if (api->abi_version!=BADGE_NATIVE_ABI||api->struct_size<sizeof *api) return -1;
    if (!api->context||api->width!=W||api->height!=H||api->top!=TOP||
        !api->now_ms||!api->wait_ms||!api->input||!api->random||!api->begin_frame||!api->submit_frame||
        !api->clear||!api->box||!api->outline||!api->text||!api->frame_idle||!api->wipe) return -2;
    int result=run_game();
    api->frame_idle(api->context);
    api=NULL;
    return result;
}

int main(int argc,char **argv) { (void)argc;(void)argv;return aliens_run(); }
