#include "badge/badge.h"
#include "badge/messaging.h"
#include "message_portal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qrcode.h"

typedef struct {
    uint8_t selected[16], active[16], anchor[16], known[MSG_CAP][16];
    int known_count;
    int index, offset;
    bool opened, phone_menu, phone_attempted, phone_ok, acknowledge;
} view_t;

static void qr(esp_qrcode_handle_t code)
{
    int n = esp_qrcode_get_size(code), scale = 240 / (n + 8);
    int left = W / 2 + (W / 2 - n * scale) / 2, top = TOP + 84 + (240 - n * scale) / 2;
    for (int y = 0; y < n; y++) for (int x = 0; x < n; x++)
        if (esp_qrcode_get_module(code, x, y)) box(left + x * scale, top + y * scale, scale, scale, true);
}
static bool wrap(int x, int y, int width, const char *p, int rows)
{
    int cols = width / 16; if (cols > 55) cols = 55;
    if (cols < 1) return *p != 0;
    while (*p && rows--) {
        char line[56]; int n = 0;
        while (p[n] && p[n] != '\n' && n < cols) n++;
        if (n == cols && p[n] && p[n] != '\n' && p[n] != ' ') {
            int end = n; while (end > 0 && p[end] != ' ') end--; if (end) n = end;
        }
        memcpy(line, p, n); line[n] = 0; text(x, y, 2, line, true); y += 22;
        p += n; if (*p == '\n' || *p == ' ') p++;
    }
    return *p != 0;
}
static void selection(const message_state_t *s, view_t *v)
{
    bool added = false;
    for (int i = 0; i < s->count; i++) {
        bool found = false;
        for (int j = 0; j < v->known_count; j++) if (!memcmp(s->messages[i].id, v->known[j], 16)) found = true;
        if (!found) added = true;
    }
    v->known_count = s->count;
    for (int i = 0; i < s->count; i++) memcpy(v->known[i], s->messages[i].id, 16);
    if (!v->opened && added) { v->index = 0; memset(v->selected, 0, 16); }
    int at = message_find(s, v->selected);
    if (at >= 0) v->index = s->count - 1 - at;
    if (v->index >= s->count) v->index = s->count ? s->count - 1 : 0;
    if (s->count) memcpy(v->selected, s->messages[s->count - 1 - v->index].id, 16);
    else memset(v->selected, 0, 16);
    if (v->opened) {
        bool exists = false;
        for (int i = 0; i < s->count; i++) if (!memcmp(v->active, s->messages[i].thread, 16)) exists = true;
        if (!exists) v->opened = false;
        int anchor = message_find(s, v->anchor); v->offset = 0;
        if (anchor >= 0) for (int i = anchor + 1; i < s->count; i++)
            if (!memcmp(v->active, s->messages[i].thread, 16)) v->offset++;
    }
}
static void age_label(const message_t *m, uint64_t now, char out[64])
{
    strcpy(out, "(time unknown)");
    if (!m->clock) return;
    if (!now) { strcpy(out, "(age unavailable)"); return; }
    if (m->seconds > now) { strcpy(out, "(future phone time)"); return; }
    uint64_t minutes = (now - m->seconds) / 60;
    if (!minutes) strcpy(out, "(just now)");
    else if (minutes < 60) snprintf(out, 64, "(%llu minute%s ago)", (unsigned long long)minutes, minutes == 1 ? "" : "s");
    else {
        uint64_t main = minutes < 1440 ? minutes / 60 : minutes / 1440;
        uint64_t rest = minutes < 1440 ? minutes % 60 : minutes / 60 % 24;
        const char *unit = minutes < 1440 ? "hour" : "day", *small = minutes < 1440 ? "minute" : "hour";
        if (rest) snprintf(out, 64, "(%llu %s%s %llu %s%s ago)", (unsigned long long)main, unit, main == 1 ? "" : "s", (unsigned long long)rest, small, rest == 1 ? "" : "s");
        else snprintf(out, 64, "(%llu %s%s ago)", (unsigned long long)main, unit, main == 1 ? "" : "s");
    }
}
static int visible_messages(const message_state_t *s, const view_t *v, int out[3])
{
    int n = 0;
    if (!v->opened) {
        int first = v->index / 3 * 3;
        for (int row = first; row < s->count && row < first + 3; row++) out[n++] = s->count - 1 - row;
    } else {
        int indices[MSG_CAP], count = 0;
        for (int i = 0; i < s->count; i++) if (!memcmp(v->active, s->messages[i].thread, 16)) indices[count++] = i;
        int offset = v->offset < count ? v->offset : count - 1, end = count - offset;
        for (int i = end > 2 ? end - 2 : 0; i < end && count; i++) out[n++] = indices[i];
    }
    return n;
}
static void visible_ages(const message_state_t *s, const view_t *v, bool available, uint64_t now, char out[3][64])
{
    memset(out, 0, 3 * 64);
    if (!available) return;
    int indices[3], count = visible_messages(s, v, indices);
    for (int i = 0; i < count; i++) age_label(&s->messages[indices[i]], now, out[i]);
}
static void card(int x, int y, int width, int height, const message_t *m, bool selected, bool unread, bool details, uint64_t now)
{
    outline(x, y, width, height, selected ? 3 : 1);
    char label[64]; snprintf(label, sizeof label, "%s%s", unread ? "* " : "", m->name[0] ? m->name : "Unnamed");
    text(x + 16, y + 12, 3, label, true);
    bool more = wrap(x + 16, y + 48, width - 32, m->body, details ? 5 : 2);
    if (details && more) text(x + 16, y + height - 48, 1, "More on phone", true);
    age_label(m, now, label);
    text(x + 16, y + height - (details ? 34 : 20), 1, label, true);
    if (details) {
        char sent[32] = "time unknown"; time_t seconds = m->seconds; struct tm utc;
        if (m->clock && gmtime_r(&seconds, &utc)) strftime(sent, sizeof sent, "%Y-%m-%d %H:%MZ phone", &utc);
        snprintf(label, sizeof label, "Badge %02x%02x%02x%02x%02x%02x / %s", m->sender[0],m->sender[1],m->sender[2],m->sender[3],m->sender[4],m->sender[5], sent);
        text(x + 16, y + height - 20, 1, label, true);
    }
}
static void connection(const view_t *v)
{
    int x = W / 2 + 20; box(W / 2 - 1, TOP, 2, H - TOP, true);
    text(x, TOP + 12, 3, v->phone_ok ? "Connect phone" : "Phone unavailable", true);
    if (!v->phone_ok) {
        wrap(x, TOP + 100, 440, "Hold both top buttons for Home, then try again. Messages are kept.", 4); return;
    }
    text(x, TOP + 52, 2, message_portal_ssid(), true);
    char payload[96]; snprintf(payload, sizeof payload, "WIFI:T:WPA;S:%s;P:%s;;", message_portal_ssid(), message_portal_password());
    esp_qrcode_config_t config = ESP_QRCODE_CONFIG_DEFAULT(); config.display_func = qr; config.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
    esp_qrcode_generate(&config, payload);
    text(x, TOP + 338, 2, "Password", true); text(x, TOP + 364, 2, message_portal_password(), true);
    text(x, TOP + 412, 2, "http://192.168.4.1", true);
    text(x, TOP + 446, 2, "Keep Wi-Fi, no internet.", true);
    text(x, TOP + 480, 2, v->opened ? "Reopen page: this reply." : "Reopen page: new message.", true);
    text(x, TOP + 508, 2, "Keep drafts before reload.", true);
    text(x, TOP + 556, 2, "Home ends phone mode.", true);
}
static void draw(const message_state_t *s, const view_t *v, bool available, int read_result, uint64_t now)
{
    int width = v->phone_menu ? W / 2 : W;
    int visible[3], nvisible = visible_messages(s, v, visible);
    memset(fb, 0, PLANE);
    text(20, TOP + 12, 3, "Messages", true);
    char label[32];
    if (available) snprintf(label, sizeof label, "%d/%d%s", s->count, MSG_CAP, s->count == MSG_CAP ? " FULL" : " saved");
    else strcpy(label, "Unavailable");
    text(width - 20 - strlen(label) * 16, TOP + 16, 2, label, true);
    if (!available) {
        ui_keys("Info", NULL, NULL, v->phone_menu ? "Close menu" : v->phone_attempted ? "Phone" : "Back");
        text(20, TOP + 88, 2, "Storage unavailable", true);
        wrap(20, TOP + 138, width - 40, messaging_status(), 5);
        wrap(20, TOP + 286, width - 40, "Existing messages have not been erased. Hold top buttons for Home.", 4);
    } else if (!v->opened) {
        ui_keys("Up", s->count ? "Open" : NULL, "Down", v->phone_menu ? "Close menu" : "Phone");
        if (s->count) {
            snprintf(label, sizeof label, "%d of %d / unverified", v->index + 1, s->count);
            text(20, TOP + 46, 2, label, true);
            for (int i = 0; i < nvisible; i++)
                card(20, TOP + 82 + i * 138, width - 40, 126, &s->messages[visible[i]], s->count - 1 - visible[i] == v->index, !s->read[visible[i]], false, now);
        } else {
            text(20, TOP + 112, 3, "No messages yet", true);
            wrap(20, TOP + 172, width - 40, "Press Phone to write a message.", 2);
        }
    } else {
        ui_keys("Older", "Reply", "Latest", v->phone_menu ? "Close menu" : "Back");
        int count = 0;
        for (int i = 0; i < s->count; i++) if (!memcmp(v->active, s->messages[i].thread, 16)) count++;
        snprintf(label, sizeof label, "%d messages / unverified", count); text(20, TOP + 46, 2, label, true);
        for (int i = 0; i < nvisible; i++) card(20, TOP + 82 + i * 220, width - 40, 210, &s->messages[visible[i]], false, false, true, now);
    }
    if (available) {
        text(20, H - 100, 2, s->count == MSG_CAP ? "Full. Delete on phone." : "Public messages", true);
        text(20, H - 72, 2, read_result > MSG_DUP ? "Read not saved; use Home" : v->phone_attempted ? v->phone_ok ? "Phone on; sharing paused" : "Phone failed; use Home" : s->relay ? "Nearby sharing ON" : "Nearby sharing OFF", true);
        text(20, H - 36, 2, "Hold top buttons: Home", true);
    }
    if (v->phone_menu) connection(v);
    ui_frame(1); ui_close();
}
static void run(void)
{
    static const uint8_t no_anchor[16] = {0};
    messaging_start();
    message_state_t *s = calloc(1, sizeof *s);
    if (!s) return;
    view_t v = {0}; bool dirty = true, was_available = false;
    uint64_t last_revision = UINT64_MAX;
    char painted_ages[3][64] = {{0}};
    for (;;) {
        uint64_t revision = last_revision;
        bool available = messaging_snapshot(s, &revision);
        if (available) selection(s, &v);
        if (revision != last_revision || available != was_available) dirty = true;
        last_revision = revision; was_available = available;
        uint64_t now = message_portal_now(); char ages[3][64];
        visible_ages(s, &v, available, now, ages);
        if (memcmp(ages, painted_ages, sizeof ages)) dirty = true;
        if (dirty) {
            // A pinned snapshot may miss a concurrent reply; only Latest mode passively reads the thread.
            bool follow_latest = !memcmp(v.anchor, no_anchor, sizeof v.anchor);
            int read_result = available && v.opened && (follow_latest || v.acknowledge) ? messaging_read(v.active) : MSG_DUP;
            v.acknowledge = false;
            draw(s, &v, available, read_result, now); memcpy(painted_ages, ages, sizeof ages); dirty = false;
        }
        int b = btn_get(1000); if (b < 0) continue;
        dirty = true;
        if (!available) {
            if (b == BTN_PLUS) ui_info();
            if (b == BTN_B) {
                if (!v.phone_attempted) { free(s); return; }
                v.phone_menu = !v.phone_menu;
            }
            continue;
        }
        if (b == BTN_B && v.phone_menu) { v.phone_menu = false; if (!v.opened) { v.index = 0; memset(v.selected, 0, 16); } continue; }
        if (!v.opened) {
            if (b == BTN_PLUS && v.index > 0) v.index--;
            if (b == BTN_A && v.index + 1 < s->count) v.index++;
            if (s->count) memcpy(v.selected, s->messages[s->count - 1 - v.index].id, 16);
            if (b == BTN_MINUS && s->count) {
                memcpy(v.active, s->messages[s->count - 1 - v.index].thread, 16); memcpy(v.anchor, v.selected, 16); v.opened = true; v.offset = 0; v.acknowledge = true;
                if (v.phone_ok) message_portal_start(v.active);
            }
            if (b == BTN_B) { v.phone_attempted = true; v.phone_ok = message_portal_start(NULL); v.phone_menu = true; }
        } else {
            if (b == BTN_PLUS) {
                int indices[MSG_CAP], count = 0;
                for (int i = 0; i < s->count; i++) if (!memcmp(v.active, s->messages[i].thread, 16)) indices[count++] = i;
                if (v.offset + 2 < count) v.offset += 2;
                if (count) memcpy(v.anchor, s->messages[indices[count - v.offset - 1]].id, 16);
            }
            if (b == BTN_A) { v.offset = 0; v.acknowledge = true; memset(v.anchor, 0, 16); }
            if (b == BTN_MINUS) { v.offset = 0; v.acknowledge = true; memset(v.anchor, 0, 16); v.phone_attempted = true; v.phone_ok = message_portal_start(v.active); v.phone_menu = true; }
            if (b == BTN_B) v.opened = false;
        }
    }
}

static void icon(int x, int y)
{
    icon_base(x, y);
    box(x + 28, y + 36, 120, 84, false);
    box(x + 44, y + 116, 24, 24, false);
    box(x + 44, y + 56, 88, 8, true);
    box(x + 44, y + 80, 64, 8, true);
}
const app_t app_messaging = {"Messages", icon, run};
const app_info_t app_messaging_info = {
    .info = "Store up to 8 public messages in threads from your phone. Relay is optional. Sender names and phone times are unverified.",
    .credit = "Code: apps/messaging + components/os/messages", .launch_info = true,
};
