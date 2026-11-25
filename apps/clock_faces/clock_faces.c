// A separate everyday clock: native digital and analog faces. The contributed apps/clock is unchanged.
// Settings: faces_face="analog" (otherwise digital), faces_24h="0" (otherwise 24-hour digital).
// The existing shared POSIX "tz" setting is read only; unset keeps the conference's US Pacific default.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "badge/badge.h"

#define TZ_DEFAULT "PST8PDT,M3.2.0,M11.1.0"
#define RESYNC_S (6 * 3600)
#define WIPE_S 3600
#define RAD 0.017453293f
static const char *TAG = "clock_faces";
static bool analog, hours24;

static void load_options(void)
{
    char value[16], tz[64];
    analog = cfg_get("faces_face", value, sizeof value) == 6 && !strcmp(value, "analog");
    hours24 = cfg_get("faces_24h", value, sizeof value) != 1 || strcmp(value, "0");
    setenv("TZ", cfg_get("tz", tz, sizeof tz) > 0 ? tz : TZ_DEFAULT, 1);
    tzset();
}

static void centred(const char *s, int y, int scale)
{
    text((W - (int)strlen(s) * 8 * scale) / 2, y - 4 * scale, scale, s, true);
}

static void clock_time(const struct tm *t, bool h24, char out[6])
{
    int h = h24 ? t->tm_hour : (t->tm_hour + 11) % 12 + 1;
    // tm comes from localtime_r. Build fixed cells directly so a one-digit hour stays in place.
    out[0] = h < 10 && !h24 ? ' ' : '0' + h / 10;
    out[1] = '0' + h % 10; out[2] = ':';
    out[3] = '0' + t->tm_min / 10; out[4] = '0' + t->tm_min % 10; out[5] = 0;
}

static void digit(int x, int y, char ch)
{
    static const uint8_t segments[] = {0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    unsigned mask = ch >= '0' && ch <= '9' ? segments[ch - '0'] : ch == '-' ? 0x40 : 0;
    if (mask & 0x01) box(x + 12, y,       72, 12, true);
    if (mask & 0x02) box(x + 84, y + 12,  12, 66, true);
    if (mask & 0x04) box(x + 84, y + 90,  12, 66, true);
    if (mask & 0x08) box(x + 12, y + 156, 72, 12, true);
    if (mask & 0x10) box(x,      y + 90,  12, 66, true);
    if (mask & 0x20) box(x,      y + 12,  12, 66, true);
    if (mask & 0x40) box(x + 12, y + 78,  72, 12, true);
}

static void digital_face(const struct tm *t)
{
    char value[6] = "--:--";
    if (t) clock_time(t, hours24, value);
    int x = (W - 492 - (hours24 ? 0 : 72)) / 2, start = x;
    for (int i = 0; i < 5; i++) {
        if (i == 2) {
            box(x + 6, 258, 12, 12, true); box(x + 6, 330, 12, 12, true); x += 36;
        } else digit(x, 210, value[i]), x += 120;
    }
    if (t && !hours24) text(start + 516, 342, 3, t->tm_hour < 12 ? "AM" : "PM", true);
}

static void stroke(int x0, int y0, int x1, int y1, int width)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    for (int err = dx + dy;;) {
        box(x0 - width / 2, y0 - width / 2, width, width, true);
        if (x0 == x1 && y0 == y1) return;
        int e = 2 * err;
        if (e >= dy) err += dy, x0 += sx;
        if (e <= dx) err += dx, y0 += sy;
    }
}

static void radial(float degrees, int r0, int r1, int width)
{
    float s = sinf(degrees * RAD), c = cosf(degrees * RAD);
    stroke(480 + lroundf(r0 * s), 340 - lroundf(r0 * c),
           480 + lroundf(r1 * s), 340 - lroundf(r1 * c), width);
}

static void analog_face(const struct tm *t)
{
    for (int m = 0; m < 60; m++) radial(m * 6, m % 5 ? 188 : 178, 196, m % 5 ? 2 : 4);
    for (int h = 1; h <= 12; h++) {
        char value[3]; snprintf(value, sizeof value, "%d", h);
        int x = 480 + lroundf(150 * sinf(h * 30 * RAD)), y = 340 - lroundf(150 * cosf(h * 30 * RAD));
        text(x - (int)strlen(value) * 12, y - 12, 3, value, true);
    }
    if (!t) { centred("--:--", 340, 5); return; }
    radial((t->tm_hour % 12) * 30 + t->tm_min / 2.0f, 0, 106, 8);
    radial(t->tm_min * 6, 0, 128, 5); // keep the hand inside the hour numbers
    box(474, 334, 12, 12, true);
}

static void draw(const struct tm *t, const char *status)
{
    char value[96];
    memset(fb, 0, sizeof fb);
    if (t) strftime(value, sizeof value, "%a, %d %b %Y", t);
    else snprintf(value, sizeof value, "Time not set");
    centred(value, 102, 3);
    if (analog) analog_face(t); else digital_face(t);
    if (t) {
        char zone[64], utc[8];
        if (!strftime(zone, sizeof zone, "%Z", t)) zone[0] = 0;
        if (!strftime(utc, sizeof utc, "%z", t)) utc[0] = 0;
        snprintf(value, sizeof value, "%.23s%s%s", zone, *utc ? " UTC" : "", utc);
        centred(value, analog ? 582 : 456, 2);
    }
    centred(status, analog ? 630 : 558, 2);
    if (!t && !analog) centred("Check Wi-Fi in Settings", 606, 2);
}

static bool valid(time_t now) { return now > 1700000000; }

static bool sync_time(void)
{
    bool ok = false;
    if (wifi_connect(20000)) {
        esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        if (esp_netif_sntp_init(&c) == ESP_OK) {
            ok = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) == ESP_OK;
            esp_netif_sntp_deinit();
        }
    }
    wifi_off(); ranging_start();
    ESP_LOGI(TAG, "time sync %s", ok ? "done" : "failed");
    return ok;
}

static void run(void)
{
    load_options();
    ui_keys("Face", "Info", "Sync", "Home");
    time_t next_sync = 0, next_wipe = time(NULL) + WIPE_S;
    time_t shown = -1;
    const char *status = "Syncing time...";
    for (;;) {
        time_t now = time(NULL), key = valid(now) ? now / 60 : -2;
        if (now >= next_wipe) ui_wipe(), next_wipe = now + WIPE_S, shown = -1;
        if (shown != key) {
            struct tm t; localtime_r(&now, &t);
            draw(valid(now) ? &t : NULL, status); ui_frame(1); ui_close(); shown = key;
        }
        if (now >= next_sync) {
            bool had_time = valid(now);
            bool ok = sync_time(); now = time(NULL);
            next_sync = now + (ok ? RESYNC_S : valid(now) ? 600 : 60);
            if (!had_time && valid(now)) next_wipe = now + WIPE_S; // don't wipe immediately after the first sync
            status = ok ? "Time synced" : valid(now) ? "Sync unavailable - using badge time" : "Time sync unavailable";
            shown = -1; continue;
        }
        int wait = valid(now) ? (60 - now % 60) * 1000 + 50 : (next_sync - now) * 1000 + 50;
        int b = btn_get(wait);
        if (b == BTN_PLUS) analog = !analog, ui_wipe(), next_wipe = time(NULL) + WIPE_S, shown = -1;
        else if (b == BTN_MINUS) ui_info(), shown = -1;
        else if (b == BTN_A) next_sync = 0, status = "Syncing time...", shown = -1;
        else if (b == BTN_B) return;
    }
}

static void icon(int x, int y)
{
    icon_base(x, y);
    for (int m = 0; m < 12; m++) {
        int px = x + ICON / 2 + lroundf(58 * sinf(m * 30 * RAD));
        int py = y + ICON / 2 - lroundf(58 * cosf(m * 30 * RAD));
        box(px - 3, py - 3, 6, 6, false);
    }
    box(x + 84, y + 42, 8, 50, false); box(x + 84, y + 84, 42, 8, false);
}

const app_t app_clock_faces = {"Clock Faces", icon, run};
const app_info_t app_clock_faces_info = {
    "Digital and analog clock faces. Face switches this session; Settings saves the default face and digital 12/24h. "
    "Wi-Fi sets time on entry and every six hours; the badge keeps time between syncs. Sync retries now.",
    "Native app: apps/clock_faces; time sync uses ESP-IDF SNTP. Original contributed Clock is separate.", false};
