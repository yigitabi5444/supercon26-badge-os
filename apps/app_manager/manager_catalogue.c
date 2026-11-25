#include "manager_ui.h"
#include <string.h>

// Metadata and pre-rendered bitmap assets never reference optional app descriptors.
const am_entry_t am_catalogue[AM_COUNT] = {
    {"codex_board", "Codex Board", 13240, "Sessions, quota and attention alerts on your desk.",
     "ml-sudocode; PR #5", 0, 0, true},
    {"messaging", "Messages", 17216, "Badge threads, replies and unread markers.",
     "evrenucar; PR #6", 1, 1, true},
    {"doom", "Doom", 328676, "A Doom engine with separately managed game data.",
     "id Software / DoomGeneric", 2, -1, true},
    {"aliens", "SpaceInv", 4928, "Move, fire and clear waves.",
     "Badge OS", 3, 2, true},
    {"rocks", "Asteroids", 13120, "Turn, thrust and fire to break drifting rocks.",
     "Badge OS", 4, -1, true},
    {"hackaday", "Hackaday", 12572, "Browse Hackaday posts and images over Wi-Fi.",
     "Badge OS / Hackaday", 5, -1, true},
    {"video", "Video", 4880, "Play a short monochrome clip.",
     "Badge OS", 6, -1, true},
};

static unsigned home_mask;
void am_home_reset(void) { home_mask=0; }
void am_home_add(const char *name)
{
    if(!name)return;
    for(unsigned i=0;i<AM_COUNT;i++)
        if(!strcmp(name,am_catalogue[i].name))home_mask|=1u<<i;
}
bool am_has_home(unsigned id) { return id<AM_COUNT && (home_mask&(1u<<id)); }
bool am_downloadable(unsigned id,const am_local_t *local)
{
    return id<AM_COUNT && am_catalogue[id].native && (!am_has_home(id) || !strcmp(am_catalogue[id].id,"aliens") ||
        local->installed || local->package_available || local->fetch_available);
}
bool am_is_builtin(unsigned id,const am_local_t *local)
{ return am_has_home(id) && !am_downloadable(id,local); }
unsigned am_group(unsigned id,const am_local_t *local)
{ return am_is_builtin(id,local)?2u:local->installed?1u:0u; }
unsigned am_order(const am_local_t local[AM_COUNT], unsigned order[AM_COUNT])
{
    unsigned n = 0;
    for (unsigned group = 0; group < 3; group++)
        for (unsigned i = 0; i < AM_COUNT; i++)
            if ((am_downloadable(i,&local[i]) || am_has_home(i)) && am_group(i,&local[i])==group) order[n++] = i;
    return n;
}

void am_next(am_view_t *view, const am_local_t local[AM_COUNT])
{
    unsigned order[AM_COUNT];
    unsigned n=am_order(local, order);
    for (unsigned i = 0; i < n; i++)
        if (order[i] == view->selected) {
            view->selected = order[(i + 1) % n];
            view->info_scroll = 0;
            view->notice = NULL;
            return;
        }
    if(n)view->selected=order[0];
}

size_t am_actions(unsigned selected, const am_local_t *local, bool ready, am_action_t out[4])
{
    size_t n = 0;
    if (selected >= AM_COUNT) return 0;
    const am_entry_t *entry = &am_catalogue[selected];
    if(!am_downloadable(selected,local)){out[0]=(am_action_t){AM_DETAILS,"Details",true};return 1;}
    if (local->installed) {
        out[n++] = (am_action_t){AM_OPEN, "Open", ready && !local->disabled};
        bool available=ready && (local->fetch_available || (local->package_available && local->update_available));
        out[n++] = (am_action_t){AM_UPDATE,local->update_available?"Update":"Check for update",available};
        out[n++] = (am_action_t){AM_UNINSTALL, "Uninstall", ready};
    } else if (entry->native) {
        bool available=ready && !local->disabled && (local->package_available || local->fetch_available);
        out[n++] = (am_action_t){AM_INSTALL,"Install",available};
    }
    out[n++] = (am_action_t){AM_DETAILS, "Details", true};
    return n;
}
