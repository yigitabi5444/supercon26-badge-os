// The native pilot uses the same drawing primitives and button layout as Badge OS.
#include <stdio.h>
#include <string.h>
#include "badge/badge.h"
#include "manager_ui.h"
#include "controller.h"
#include "controller_target.h"
#include "video_asset.h"

enum { LIST_Y = TOP + 56, LIST_END = H - 72, RIGHT = 416, EDGE = W - 24 };
typedef struct { int x, y, right, bottom; } clip_t;
static const clip_t list_clip = {16, LIST_Y, 384, LIST_END};
static const clip_t info_clip = {RIGHT, TOP + 64, EDGE, LIST_END};
static am_controller_t controller; // Stable owner for the manager and its synchronous Home shortcut.

static void clipped_box(clip_t c, int x, int y, int w, int h, bool ink)
{
    int right = x + w, bottom = y + h;
    if (x < c.x) x = c.x;
    if (y < c.y) y = c.y;
    if (right > c.right) right = c.right;
    if (bottom > c.bottom) bottom = c.bottom;
    if (right > x && bottom > y) box(x, y, right - x, bottom - y, ink);
}

static void clipped_text(clip_t c, int x, int y, int scale, const char *s, bool ink)
{
    if (y < c.y || y + 8 * scale > c.bottom) return;
    for (; *s; s++, x += scale * 8) {
        char glyph[2] = {*s, 0};
        if (x >= c.x && x + scale * 8 <= c.right) text(x, y, scale, glyph, ink);
    }
}

static int paragraph(clip_t c, int y, const char *s, int scale)
{
    int columns = (c.right - c.x) / (scale * 8);
    while (*s) {
        char line[64];
        int n = 0;
        while (*s && *s != '\n' && n < columns && n < 63) line[n++] = *s++;
        if (n == columns && *s && *s != '\n') {
            int last = n;
            while (last > 0 && line[last - 1] != ' ') last--;
            if (last) { s -= n - last; n = last - 1; }
        }
        line[n] = 0;
        if (*s == '\n') s++;
        while (*s == ' ') s++;
        clipped_text(c, c.x, y, scale, line, true);
        y += 8 * scale + 8;
    }
    return y;
}

static void bitmap(clip_t c, const uint8_t *bits, int width, int height, int x, int y, int scale)
{
    clipped_box(c, x, y, width * scale, height * scale, false);
    for (int row = 0; row < height; row++)
        for (int col = 0; col < width; col++)
            if (bits[row * (width / 8) + col / 8] & (0x80 >> (col & 7)))
                clipped_box(c, x + col * scale, y + row * scale, scale, scale, true);
}

static void package_size(unsigned id,const am_local_t *local,char out[24])
{
    bool approximate=false;uint64_t bytes;
    if(local->installed && local->size)bytes=local->size;
    else if(local->package_available && local->package_size)bytes=local->package_size;
    else {bytes=am_catalogue[id].package_bytes_hint;approximate=true;}
    if(!strcmp(am_catalogue[id].id,"video"))bytes+=AM_VIDEO_CLIP_BYTES;
    snprintf(out,24,"%s%lu KiB",approximate?"~":"",(unsigned long)((bytes+1023)/1024));
}

static void list(am_view_t *v, const am_local_t local[AM_COUNT])
{
    unsigned order[AM_COUNT];
    int y = 0, selected_y = 0, selected_h = 0;
    unsigned count=am_order(local, order);
    unsigned previous = 3;
    for (unsigned k = 0; k < count; k++) {
        unsigned id = order[k];
        unsigned group=am_group(id,&local[id]);
        if (group != previous) y += 30;
        if (id == v->selected) { selected_y = y; selected_h = 60; }
        y += 60;
        previous = group;
    }
    int height = LIST_END - LIST_Y, limit = y > height ? y - height : 0;
    if (selected_y < (int)v->list_scroll) v->list_scroll = selected_y;
    if (selected_y + selected_h > (int)v->list_scroll + height)
        v->list_scroll = selected_y + selected_h - height;
    if (v->list_scroll > (unsigned)limit) v->list_scroll = limit;
    text(16, TOP + 16, 3, "Apps", true);
    y = LIST_Y - v->list_scroll;
    previous = 3;
    for (unsigned k = 0; k < count; k++) {
        unsigned id = order[k];
        const am_entry_t *a = &am_catalogue[id];
        unsigned group=am_group(id,&local[id]);
        bool installed=group==1,selected=id==v->selected;
        if (group != previous) {
            const char *headers[]={"Downloads","Installed","Built-in"};
            clipped_text(list_clip, 16, y + 4, 2, headers[group], true);
            y += 30;
        }
        clipped_box(list_clip, 16, y, 368, 56, selected);
        clipped_box(list_clip, 16, y, 368, 1, !selected);
        bitmap(list_clip, am_icons[a->icon], 32, 32, 28, y + 12, 1);
        int x = 76;
        char size[24];package_size(id,&local[id],size);
        int size_x=list_clip.right-(int)strlen(size)*16;
        clip_t name_clip=list_clip;name_clip.right=size_x-8;
        clipped_text(name_clip, x, y + 8, 2, a->name, !selected);
        clipped_text(list_clip, size_x, y + 8, 2, size, !selected);
        clip_t version_clip = list_clip;
        if (installed && local[id].update_available) version_clip.right = 256;
        char downloaded[48];const char *status="Not downloaded";
        if(am_is_builtin(id,&local[id]))status="Built-in";
        else if(installed)status=local[id].version?local[id].version:"Installed";
        else if(local[id].package_available) {
            snprintf(downloaded,sizeof downloaded,"Downloaded%s%s",local[id].package_version?" ":"",
                     local[id].package_version?local[id].package_version:"");status=downloaded;
        }
        clipped_text(version_clip, x, y + 32, 2, status, !selected);
        if (installed && local[id].update_available)
            clipped_text(list_clip, 272, y + 32, 2, "Update", !selected);
        y += 60;
        previous = group;
    }
    box(400, TOP + 16, 2, LIST_END - TOP - 16, true);
    if (limit) {
        int thumb = height * height / (height + limit);
        box(392, LIST_Y, 2, height, true);
        box(389, LIST_Y + (height - thumb) * v->list_scroll / limit, 8, thumb, true);
    }
}

static unsigned details(am_view_t *v, const am_local_t local[AM_COUNT])
{
    const am_entry_t *a = &am_catalogue[v->selected];
    int offset = (int)v->info_scroll, y = info_clip.y - offset;
    text(RIGHT, TOP + 16, 3, a->name, true);
    y = paragraph(info_clip, y, a->description, 2) + 16;
    if(am_downloadable(v->selected,&local[v->selected]) && am_has_home(v->selected))
        y=paragraph(info_clip,y,"Built-in copy on Home.",2)+16;
    if (a->shot >= 0) bitmap(info_clip, a->shot<2?am_shots[a->shot]:am_native_shots[a->shot-2], 160, 112, RIGHT, y, 1);
    else clipped_text(info_clip, RIGHT, y + 40, 2, "Preview pending", true);
    clip_t meta = {RIGHT + 184, info_clip.y, EDGE, info_clip.bottom};
    const am_local_t empty={0};
    bool built_in=am_is_builtin(v->selected,&local[v->selected]);
    const am_local_t *p = built_in?&empty:&local[v->selected];
    clipped_text(meta, meta.x, y, 3, built_in?"Built-in":p->installed ? "Installed" : a->native ? p->package_available?"Downloaded":"Not downloaded" : "Source only", true);
    if (p->installed) clipped_text(meta, meta.x, y + 32, 2, p->version, true);
    else if(p->package_available)clipped_text(meta,meta.x,y+32,2,p->package_version,true);
    char size[24];package_size(v->selected,p,size);clipped_text(meta,meta.x,y+64,2,size,true);
    bool video=!strcmp(a->id,"video");
    if(video)clipped_text(meta,meta.x,y+88,1,"Includes clip",true);
    if(p->disabled)clipped_text(meta,meta.x,y+96,2,"Disabled",true);
    if(p->update_available)clipped_text(meta,meta.x,y+(video || p->disabled?112:96),2,"Update available",true);
    y += 136;
    y = paragraph(info_clip, y, a->credit, 2) + 16;
    if (p->installed) y = paragraph(info_clip, y, p->disabled?"Uninstall":"Open / Uninstall", 2) + 24;
    else if (am_downloadable(v->selected,&local[v->selected])) {
        clipped_text(info_clip, RIGHT, y, 3, "About", true);
        y = paragraph(info_clip, y + 40, p->package_available?"Package ready.":
            p->fetch_available?"Install downloads and verifies.":"Package source unavailable.", 2) + 16;
    }
    if (v->notice) y = paragraph(info_clip, y, v->notice, 2) + 16;
    unsigned content = y + offset - info_clip.y, height = info_clip.bottom - info_clip.y;
    unsigned max = content > height ? content - height : 0;
    if (v->info_scroll > max) {
        v->info_scroll = max;
        box(RIGHT, info_clip.y, EDGE - RIGHT, info_clip.bottom - info_clip.y, false);
        return details(v, local); // At most one retry after clamping stale scroll state.
    }
    if (max) {
        int thumb = height * height / content;
        box(W - 10, info_clip.y, 2, height, true);
        box(W - 14, info_clip.y + (height - thumb) * v->info_scroll / max, 10, thumb, true);
    }
    return max;
}

static void storage(void)
{
    box(16, H - 58, W - 32, 58, false);
    box(16, H - 58, W - 32, 1, true);
}
static void storage_status(const am_controller_t *c)
{
    storage();
    const clip_t footer = {16, H - 56, W - 16, H};
    if (!c || c->state != AM_READY || !c->capacity || c->capacity > 0x800000 ||
        c->used > c->capacity) {
        clipped_text(footer, 16, H - 44, 2,
            c && c->state == AM_UNASSIGNED ? "App store unassigned" : "App store unavailable", true);
        return;
    }
    uint64_t free = c->capacity - c->used;
    uint64_t reserve = c->store.reserve_bytes ? c->store.reserve_bytes : 4096;
    if (reserve > free) reserve = free;
    const uint64_t amounts[] = {c->used, reserve, free - reserve};
    enum { BAR_X = 177, BAR_Y = H - 51, BAR_W = W - 194, BAR_H = 16 };
    // The physical 8 MiB bound above makes both uint64 products safe.
    int end[] = {(int)(c->used * BAR_W / c->capacity),
                 (int)((c->used + reserve) * BAR_W / c->capacity)};
    text(16, H - 52, 2, "App store", true);
    box(BAR_X - 1, BAR_Y - 1, BAR_W + 2, BAR_H + 2, true);
    box(BAR_X, BAR_Y, BAR_W, BAR_H, false);
    for (int row = 0; row < BAR_H; row++)
        for (int col = 0; col < BAR_W; col++) {
            int k = col < end[0] ? 0 : col < end[1] ? 1 : 2;
            bool ink = k == 0 ? (row + col) % 2 == 0 :
                       k == 1 ? (row + col) % 4 == 0 : !(row % 4 || col % 4);
            if (ink) box(BAR_X + col, BAR_Y + row, 1, 1, true);
        }
    const char *labels[] = {"Used", "Reserved", "Available"};
    for (int k = 0; k < 3; k++) {
        int x = 16 + k * (W - 48) / 3;
        for (int row = 0; row < 16; row++)
            for (int col = 0; col < 16; col++) {
                bool ink = k == 0 ? (row + col) % 2 == 0 :
                           k == 1 ? (row + col) % 4 == 0 : !(row % 4 || col % 4);
                if (ink) box(x + col, H - 28 + row, 1, 1, true);
            }
        // Controller used includes filesystem overhead; never round available up.
        unsigned kib = amounts[k] / 1024 + (k < 2 && amounts[k] % 1024 != 0);
        char label[32];
        snprintf(label, sizeof label, "%s %u KiB", labels[k], kib);
        text(x + 24, H - 28, 2, label, true);
    }
}

static void network_status(int x,int y,int scale)
{
    char status[64];
    snprintf(status,sizeof status,"Wi-Fi: %s | Time: %s",wifi_connected()?"connected":"off",
             am_https_clock_ready()?"ready":"needs sync");
    text(x,y,scale,status,true);
}
static void progress(const char *message)
{
    ui_keys(NULL,NULL,NULL,NULL);
    box(0,TOP,W,H-TOP,false);
    text(32,TOP+24,3,"App Manager",true);
    network_status(32,TOP+80,2);
    text(32,TOP+136,2,message,true);
    ui_frame(1);ui_close(); // Status is visible and the panel is off before network waits.
}
static const char *action_notice(enum am_action action,am_result_t result)
{
    if(result!=AM_OK)return am_controller_notice(result);
    return action==AM_INSTALL?"Installed":action==AM_UPDATE?"Updated":NULL;
}
static int browse_key(void)
{
    int key=btn_get(800); // Reuse the calm session during quick successive presses.
    if(key<0){ui_close();key=btn_get(-1);} // Power down before an indefinite idle wait.
    return key;
}
void app_manager_ui(void)
{
    am_view_t v = {0};
    for(unsigned i=0;i<AM_COUNT;i++)if(am_catalogue[i].native){v.selected=i;break;}
    ui_close();
    am_result_t started=am_target_start(&controller);
    if(started!=AM_OK && started!=AM_DISABLED)v.notice=am_controller_notice(started);
    const am_local_t *local=controller.local;
    bool prepare=true;
    for (;;) {
        if(controller.execution_copy || controller.lease.owner) {
            // A timed-out drawer may still own fb: do not draw another screen until quiescent.
            if(am_controller_shutdown(&controller)==AM_OK)return;
            btn_get(20);continue; // Immediate OS Home remains available.
        }
        if(controller.exit_requested) {
            ui_close();
            if(am_controller_shutdown(&controller)==AM_OK) {
                while(btn_held()&((1<<BTN_PLUS)|(1<<BTN_MINUS)))btn_get(20);
                btn_flush();return;
            }
            controller.exit_requested=controller.home_pending=false;
            v.notice="Storage unavailable"; // Default immediate Home remains the escape.
        }
        ui_keys(v.menu ? "Back" : "Actions", v.menu ? "Up" : "Info up",
                v.menu ? "Select" : "Next app", v.menu ? "Down" : "Info down");
        box(0, TOP, W, H - TOP, false);
        list(&v, local);
        unsigned max = details(&v, local);
        network_status(RIGHT,TOP+48,1);
        am_action_t choices[4];
        size_t n = am_actions(v.selected, &local[v.selected], controller.state==AM_READY && !controller.busy, choices);
        if (v.menu) {
            box(RIGHT, info_clip.y, EDGE - RIGHT, LIST_END - info_clip.y, false);
            for (size_t i = 0; i < n; i++) {
                int y = info_clip.y + 24 + i * 60;
                box(RIGHT, y, EDGE - RIGHT, 48, v.action == i);
                text(RIGHT + 12, y + 16, 2, choices[i].label, v.action != i);
            }
        }
        storage_status(&controller);
        ui_frame(1);
        if(prepare) {
            prepare=false;
            bool ready=am_https_clock_ready();
            am_result_t result=am_target_prepare(&controller,progress);
            if(result!=AM_OK && result!=AM_DISABLED)v.notice=am_controller_notice(result);
            if(result!=AM_DISABLED && (!ready || result!=AM_OK))continue;
        }
        int key = browse_key();
        if (v.menu) {
            if (key == BTN_PLUS) v.menu = false;
            if (key == BTN_MINUS && v.action) v.action--;
            if (key == BTN_B && v.action + 1 < n) v.action++;
            if (key == BTN_A) {
                if(choices[v.action].enabled) {
                    ui_close();
                    am_result_t r=am_controller_action(&controller,v.selected,choices[v.action].kind);
                    v.notice=action_notice(choices[v.action].kind,r);
                } else v.notice="Unavailable";
                v.menu = false;
            }
        } else {
            if (key == BTN_PLUS) { v.menu = true; v.action = 0; }
            if (key == BTN_A) am_next(&v, local);
            if (key == BTN_MINUS) v.info_scroll = v.info_scroll > 72 ? v.info_scroll - 72 : 0;
            if (key == BTN_B) v.info_scroll = v.info_scroll + 72 < max ? v.info_scroll + 72 : max;
        }
    }
}
void am_manager_open_installed(unsigned id)
{
    if(id>=AM_COUNT)return;
    am_result_t r=am_target_start(&controller);
    if(r==AM_BUSY && !controller.opened && !controller.store.work &&
       !controller.execution_copy && !controller.lease.owner)return; // No owned context to shut down.
    if(r==AM_OK)am_controller_action(&controller,id,AM_OPEN);
    while(am_controller_shutdown(&controller)!=AM_OK)btn_get(20);
}
