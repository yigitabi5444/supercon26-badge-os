// Installed-only shortcuts. Receipts remain authoritative; this is no package database.
#include <string.h>
#include "badge/badge.h"
#include "home_apps.h"
#include "controller_target.h"
extern const app_t *const apps[];
extern const int napps;
_Static_assert(AM_COUNT==7,"Frozen Home shortcut maximum");
#if defined(__APPLE__)
#define OPTIONAL __attribute__((weak_import))
#else
#define OPTIONAL __attribute__((weak))
#endif
extern const app_t app_codex_board OPTIONAL,app_messaging OPTIONAL,app_doom OPTIONAL,app_aliens OPTIONAL,
    app_rocks OPTIONAL,app_hackaday OPTIONAL,app_video OPTIONAL;
static const app_t *const original[AM_COUNT]={&app_codex_board,&app_messaging,&app_doom,&app_aliens,&app_rocks,&app_hackaday,&app_video};
static bool same_app(const app_t *core,unsigned id)
{
    return core && (core==original[id] || (core->name &&
        (!strcmp(core->name,am_catalogue[id].name) || !strcmp(core->name,am_catalogue[id].id))));
}
static void draw_icon(unsigned id,int x,int y)
{
    icon_base(x,y);box(x+24,y+24,128,128,false);
    const uint8_t *bits=am_icons[am_catalogue[id].icon];
    for(int row=0;row<32;row++)for(int col=0;col<32;col++)
        if(bits[row*4+col/8]&(0x80>>(col&7)))box(x+24+col*4,y+24+row*4,4,4,true);
}
#define SHORTCUT(id) static void icon_##id(int x,int y){draw_icon(id,x,y);} \
    static void run_##id(void){am_manager_open_installed(id);}
SHORTCUT(0) SHORTCUT(1) SHORTCUT(2) SHORTCUT(3) SHORTCUT(4) SHORTCUT(5) SHORTCUT(6)
static app_t shortcuts[AM_COUNT]={
    {.icon=icon_0,.run=run_0},{.icon=icon_1,.run=run_1},{.icon=icon_2,.run=run_2},{.icon=icon_3,.run=run_3},
    {.icon=icon_4,.run=run_4},{.icon=icon_5,.run=run_5},{.icon=icon_6,.run=run_6}};
size_t am_home_apps_inventory(const app_t **out,size_t capacity)
{
    if(!out || !capacity)return 0;
    if(capacity>AM_COUNT)capacity=AM_COUNT;
    for(size_t i=0;i<capacity;i++)out[i]=NULL;
    unsigned wanted=0;
    for(unsigned id=0;id<AM_COUNT;id++) {
        bool core=false;for(int i=0;i<napps;i++)if(same_app(apps[i],id)){core=true;break;}
        if(!core)wanted|=1u<<id;
    }
    if(!wanted)return 0;
    bool installed[AM_COUNT];
    if(am_target_home_inventory(wanted,installed)!=AM_OK)return 0;
    size_t count=0;
    for(unsigned id=0;id<AM_COUNT && count<capacity;id++)if(installed[id]) {
        shortcuts[id].name=am_catalogue[id].name;out[count++]=&shortcuts[id];
    }
    return count;
}
