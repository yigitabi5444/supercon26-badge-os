// Display: apps draw into fb; ui_frame draws the top bar over rows 0..TOP-1, waits for the previous pull, sends the
// whole frame and starts its calm pull. The session (DC-DC up, the calm table latched) stays open between frames;
// ui_close ends it. The panel itself is epd.c.
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cricket.h"
#include "badge/badge.h"
#include "badge/os.h"
#include "epd.h"
#include "draw_into.h"
#include <sys/param.h>

static const char *TAG = "os";
EXT_RAM_BSS_ATTR uint8_t fb[PLANE]; // in PSRAM: internal RAM is needed for Wi-Fi and TLS (Online update)
static EXT_RAM_BSS_ATTR uint8_t shown[PLANE]; // what the film shows; ui_wipe leaves both all white
static int pulls = -1;               // pulls in the open session, -1 = no session
static bool pulling;                 // a pull may still run: epd_wait before the next command
static SemaphoreHandle_t lock;       // one panel user at a time: apps draw from more than one task
static const char *bar_app = "Home"; // the name in the top bar
static const char *bar_info, *bar_credit;
static EXT_RAM_BSS_ATTR uint8_t info_fb[PLANE];
static bool info_visible; // protected by lock; card pixels are independent of app/background drawing
static const char *keys[4];          // ui_keys: what BTN_PLUS, BTN_MINUS, BTN_A, BTN_B do now
volatile bool in_app;                // an app is running: MENU + and MENU - held together go home

// The same Wi-Fi glyph with a slash when there is no infrastructure IP. Ranging/SoftAP never sets this bit.
static void draw_wifi(uint8_t *dst, int x, int y)
{
    static const uint32_t glyph[16] = {
        0x01FF80, 0x0FFFF0, 0x3E007C, 0xF0000F, 0x61FF86, 0x03FFC0, 0x0F00F0, 0x060060,
        0x007E00, 0x00FF00, 0x00C300, 0, 0x001800, 0x003C00, 0x003C00, 0x001800,
    };
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 24; c++)
            if (glyph[r] & (1U << (23 - c))) box_into(dst, x + c, y + r, 1, 1, true);
    bool online = wifi_connected();
    if (!online) { // white clearance keeps the diagonal readable over the arcs
        for (int c = 0; c < 24; c++) box_into(dst, x + c - 1, y + 14 - c * 14 / 23 - 1, 4, 4, false);
        for (int c = 0; c < 24; c++) box_into(dst, x + c, y + 14 - c * 14 / 23, 2, 2, true);
    }
}

// A compact envelope and unread count. Only reads a lock-free cache; never starts a refresh.
static void draw_messages(uint8_t *dst, int x, int y)
{
    box_into(dst, x, y + 1, 20, 14, true);
    box_into(dst, x + 1, y + 2, 18, 12, false);
    for (int c = 1; c < 10; c++) {
        box_into(dst, x + c, y + 2 + c / 2, 1, 1, true);
        box_into(dst, x + 19 - c, y + 2 + c / 2, 1, 1, true);
    }
    int unread = messaging_unread();
    char count[2] = {unread >= 0 && unread <= MSG_CAP ? '0' + unread : '?', 0};
    text_into(dst, x + 24, y, 2, count, true);
}

// Top bar (rows 0..TOP-1) on every screen, drawn over fb by ui_frame. The buttons sit two in each top corner,
// MENU + above A on the left, MENU - above B on the right: each label (ui_keys) sits beside its button, with a tab at
// the edge pointing at it. In the middle: the app's name, then the username, OS version and battery.
static void draw_bar(uint8_t *dst, const char *const labels[4])
{
    static const int ky[4] = {4, 4, 26, 26}; // label rows of BTN_PLUS, BTN_MINUS, BTN_A, BTN_B
    static int pct = -2;
    static int64_t next;
    static char name[13];
    if (esp_timer_get_time() >= next) { // every 30 s, so the bar sits still
        pct = battery_pct();
        cfg_get("name", name, sizeof name);
        next = esp_timer_get_time() + 30000000;
    }
    char mid[64], bat[16] = "USB";
    int n = snprintf(mid, sizeof mid, "%s%sv%s", name, *name ? "  " : "", esp_app_get_description()->version);
    if (pct >= 0) snprintf(bat, sizeof bat, "%d%%", pct);
    bool spk = !in_app && cricket_version() >= 1; // home screen: a speaker Cricket is plugged in
    int x = (W - n * 16 - (pct == -2 ? 0 : 32 + 38 + (int)strlen(bat) * 16) - (spk ? 40 : 0)) / 2; // text, battery, speaker
    box_into(dst, 0, 0, W, TOP, false);
    for (int k = 0; k < 4; k++) {
        if (!labels[k] || !*labels[k]) continue;
        bool left = k == BTN_PLUS || k == BTN_A;
        box_into(dst, left ? 0 : W - 6, ky[k], 6, 16, true);
        text_into(dst, left ? 14 : W - 14 - (int)strlen(labels[k]) * 16, ky[k], 2, labels[k], true);
    }
    int title_x = (W - (int)strlen(bar_app) * 16 - 92) / 2;
    text_into(dst, title_x, 4, 2, bar_app, true);
    draw_wifi(dst, title_x + (int)strlen(bar_app) * 16 + 16, 4);
    draw_messages(dst, title_x + (int)strlen(bar_app) * 16 + 52, 4);
    text_into(dst, x, 26, 2, mid, true);
    x += n * 16;
    if (pct != -2) { // a battery outline filled to the charge (full on USB), then "87%" or "USB"
        x += 32;
        box_into(dst, x, 27, 30, 14, true);
        box_into(dst, x + 2, 29, 26, 10, false);
        box_into(dst, x + 30, 31, 3, 6, true);
        box_into(dst, x + 4, 31, pct < 0 ? 22 : 22 * pct / 100, 6, true);
        text_into(dst, x + 38, 26, 2, bat, true);
        x += 38 + (int)strlen(bat) * 16;
    }
    if (spk) { // a speaker: body, cone, one sound wave
        x += 18;
        box_into(dst, x, 31, 6, 8, true);
        for (int k = 0; k < 8; k++) box_into(dst, x + 6 + k, 31 - k / 2, 1, 8 + k, true);
        box_into(dst, x + 18, 29, 2, 12, true);
    }
    box_into(dst, 0, TOP - 4, W, 2, true);
}

void ui_keys(const char *plus, const char *minus, const char *a, const char *b)
{
    keys[BTN_PLUS] = plus, keys[BTN_MINUS] = minus, keys[BTN_A] = a, keys[BTN_B] = b;
}

// The pull in flight done, the session closed. Under the lock.
static void panel_off(void)
{
    if (pulling) epd_wait(), pulling = false;
    if (pulls >= 0) epd_close();
    pulls = -1;
}

// Show one explicit frame under the lock; the current driver/session logic is unchanged.
static void frame(const uint8_t *pixels)
{
    if (memcmp(pixels, shown, PLANE)) { // no change, no refresh
        int64_t t0 = esp_timer_get_time();
        if (pulling) epd_wait(), pulling = false;
        int64_t t1 = esp_timer_get_time();
        if (pulls == (epd_is84() ? 20 : 60)) panel_off(); // a fresh session: B1 60 pulls (proven), 84 20 (the badge team's)
        if (pulls < 0) {
            epd_reset();
            if (epd_is84()) epd_load(shown, shown); // the 84 takes its planes before the register sequence, as the badge team's
            epd_start(EPD_CALM);
            if (!epd_is84()) epd_load(shown, shown); // 0x11 must hold what the film shows
            pulls = 0;
        }
        epd_frame(pixels, shown);
        memcpy(shown, pixels, PLANE);
        pulls++, pulling = true;
        ESP_LOGD(TAG, "frame %d: %d ms waiting for the panel, %d ms sending", pulls, (int)((t1 - t0) / 1000),
                 (int)((esp_timer_get_time() - t1) / 1000));
    }
}

void ui_frame(int unused)
{
    (void)unused;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (info_visible) { xSemaphoreGive(lock); ms(1); return; }
    draw_bar(fb, keys);
    frame(fb);
    xSemaphoreGive(lock);
}

// Frames leave light ghosts of erased ink; a scene change shakes the film with the short black/white waveform (all
// black, then all white, ~2.8 s) and leaves the panel and the driver agreeing on white.
void ui_wipe(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (info_visible) { xSemaphoreGive(lock); return; }
    panel_off();
    memset(fb, 0xFF, sizeof fb);
    epd_reset();
    epd_load(fb, NULL);
    epd_start(EPD_FAST);
    epd_pull(5000);
    epd_load(NULL, NULL); // both planes: after the pull 0x11 holds the black frame, which would select red
    epd_pull(5000);
    epd_close();
    memset(fb, 0, sizeof fb);
    memset(shown, 0, sizeof shown);
    xSemaphoreGive(lock);
}

void ui_close(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    panel_off();
    xSemaphoreGive(lock);
}

// A compact card; wrap prose at word boundaries and bound its rows below TOP.
static void info_lines(const char *s, int y, int scale, int rows)
{
    if (!s) return;
    int cols = (W - 96) / (8 * scale);
    for (int row = 0; *s && row < rows; row++, y += 8 * scale + 12) {
        while (*s == ' ') s++;
        int n = 0;
        while (s[n] && s[n] != '\n' && n < cols) n++;
        if (n == cols && s[n] && s[n] != ' ' && s[n] != '\n') {
            int end = n;
            while (end > 0 && s[end] != ' ') end--;
            if (end) n = end;
        }
        char line[55]; // scale >= 2: at most 54 columns
        memcpy(line, s, n);
        line[n] = 0;
        text_into(info_fb, 48, y, scale, line, true);
        s += n;
        if (*s == '\n') s++;
    }
}

static void show_info(int timeout_ms, const volatile bool *dismiss)
{
    if (!bar_info || (dismiss && *dismiss)) return;
    const char *label = timeout_ms < 0 ? "Back" : "Skip";
    const char *labels[4] = {label, label, label, label};
    xSemaphoreTake(lock, portMAX_DELAY);
    if (info_visible) { xSemaphoreGive(lock); return; }
    info_visible = true;
    memset(info_fb, 0, sizeof info_fb);
    text_into(info_fb, 48, TOP + 40, 5, bar_app, true);
    info_lines(bar_info, TOP + 128, 3, 4);
    info_lines(bar_credit, TOP + 340, 2, 2);
    text_into(info_fb, 48, H - 132, 2, "Source: github.com/yigitabi5444/supercon26-badge-os", true);
    text_into(info_fb, 48, H - 72, 2, "Hold MENU + and MENU - to return home", true);
    text_into(info_fb, 48, H - 40, 2, timeout_ms < 0 ? "Any button: back" : "Any button: skip", true);
    draw_bar(info_fb, labels);
    frame(info_fb);
    if (pulling) epd_wait(), pulling = false; // dwell starts only after the card is visible
    // Start the launch timeout only once the card is visible; include the normal safe shutdown in that time.
    int64_t visible_at = esp_timer_get_time();
    panel_off();
    xSemaphoreGive(lock);
    int wait = timeout_ms < 0 ? -1 : MAX(0, timeout_ms - (int)((esp_timer_get_time() - visible_at) / 1000));
    while (!dismiss || !*dismiss) {
        if (btn_get(dismiss ? 200 : wait) >= 0 || timeout_ms >= 0) break;
    }
    // Queue delivery precedes held's update in the button task: allow one scan, then wait for release.
    // This also keeps the Home chord active and prevents a held Skip becoming game input.
    ms(20);
    while (btn_held() && !(dismiss && *dismiss)) ms(20);
    btn_flush();
    xSemaphoreTake(lock, portMAX_DELAY);
    info_visible = false;
    if (timeout_ms < 0) { // runtime Info restores the latest app frame; launch Info is followed by the existing wipe
        draw_bar(fb, keys);
        frame(fb);
        panel_off();
    }
    xSemaphoreGive(lock);
}

void ui_info(void) { show_info(-1, NULL); }
void ui_info_until(const volatile bool *dismiss) { show_info(-1, dismiss); }

void os_launch_info(void) { show_info(3000, NULL); }

// Every restart (going home, an app's esp_restart) lets the pull in flight finish and closes the session: never cut
// the panel off mid-refresh. The lock stays taken: no frame after this one.
static void panel_shutdown(void)
{
    if (xSemaphoreTake(lock, pdMS_TO_TICKS(3000))) panel_off();
}

void os_app(const app_t *app, const app_info_t *info)
{
    bar_app = app ? app->name : "Home";
    bar_info = info ? info->info : NULL;
    bar_credit = info ? info->credit : NULL;
    in_app = app != NULL;
}

void display_init(void)
{
    epd_init();
    lock = xSemaphoreCreateMutex();
    esp_register_shutdown_handler(panel_shutdown);
}
