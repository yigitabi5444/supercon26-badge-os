// Home screen: an iOS-style grid of app icons, 8 to a page. MENU + / MENU - move left / right, A or B opens the app.
// An app runs on this task; when it returns, the badge gets back what the app may have taken: USB (a restart if the
// app made it a host, so the badge stays flashable), sound, the radio (ranging) and the LEDs.
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "driver/usb_serial_jtag.h"
#include "usb/usb_host.h"
#include "badge/badge.h"
#include "badge/os.h"
#include "home.h"

static const char *TAG = "os";

#define PITCH 224 // icon to icon
#define X0 56     // (960 - 4 * 176 - 3 * 48) / 2: four icons per row
#define Y0 80     // below the top bar
#define ROW 300

// App Manager supplies resident installed-only descriptors, never pointers into an ELF.
extern size_t app_manager_home_apps(const app_t **out, size_t capacity) __attribute__((weak));
enum { HOME_EXTRA_MAX = 7 };

static const app_t *home_app(int i, const app_t *const extra[])
{ return i < napps ? apps[i] : extra[i - napps]; }

static unsigned refresh_home_apps(const app_t **extra, int *sel)
{
    if (!app_manager_home_apps) return 0;
    const app_t *selected = home_app(*sel, extra), *found[HOME_EXTRA_MAX] = {0};
    size_t count = app_manager_home_apps(found, HOME_EXTRA_MAX);
    if (count > HOME_EXTRA_MAX) count = HOME_EXTRA_MAX;
    unsigned n = 0;
    for (size_t i = 0; i < count; i++) {
        const app_t *a = found[i];
        if (!a || !a->name || !a->icon || !a->run) continue;
        bool duplicate = false;
        for (int j = 0; j < napps; j++)
            duplicate |= a == apps[j] || !strcmp(a->name, apps[j]->name);
        for (unsigned j = 0; j < n; j++)
            duplicate |= a == extra[j] || !strcmp(a->name, extra[j]->name);
        if (!duplicate) extra[n++] = a;
    }
    *sel = 0;
    for (int i = 0; i < napps + (int)n; i++)
        if (home_app(i, extra) == selected) { *sel = i; break; }
    return n;
}

static void draw_home(int sel, const app_t *const extra[], unsigned extra_count)
{
    char ver[48];
    snprintf(ver, sizeof ver, "badge os v%s", esp_app_get_description()->version);
    memset(fb, 0, sizeof fb);
    int count = napps + (int)extra_count;
    int page = sel / 8, pages = (count + 7) / 8; // 8 icons a page; moving past the last one turns the page
    for (int i = page * 8; i < MIN(count, page * 8 + 8); i++) {
        int x = X0 + i % 4 * PITCH, y = Y0 + i % 8 / 4 * ROW;
        const app_t *a = home_app(i, extra);
        a->icon(x, y);
        int width = (int)strlen(a->name) * 8, scale = 3;
        while (scale > 1 && width * scale > PITCH - 24) scale--;
        text(x + ICON / 2 - width * scale / 2, y + ICON + 24, scale, a->name, true);
        if (i == sel) outline(x - 16, y - 16, ICON + 32, ICON + 32, 8);
    }
    text(W - (int)strlen(ver) * 24 - 24, H - 48, 3, ver, true);
    for (int p = 0; pages > 1 && p < pages; p++) { // page dots, bottom centre: the current one filled
        int x = W / 2 - pages * 16 + p * 32;
        if (p == page) box(x, H - 44, 16, 16, true);
        else outline(x, H - 44, 16, 16, 3);
    }
}

void home(void)
{
    ui_keys("< Left", "Right >", "Open", "Open");
    btn_flush();
    const app_t *extra[HOME_EXTRA_MAX] = {0};
    unsigned extra_count = 0;
    for (int sel = 0, first = 1;;) {
        int count = napps + (int)extra_count;
        draw_home(sel, extra, extra_count);
        ui_frame(1);
        if (first) { // Show core Home and validate boot before any optional store scan.
            esp_ota_mark_app_valid_cancel_rollback();
            first = 0;
            extra_count = refresh_home_apps(extra, &sel);
            if (extra_count) continue;
        }
        int b = btn_get(10000), open = 0;
        if (b < 0) { // the screen sits still: end the panel session, then wait as long as it takes
            ui_close();
            b = btn_get(-1);
        }
        // Every press counts at once, also those made during the last refresh; the next frame shows where they led.
        for (; b >= 0 && !open; b = btn_get(0)) {
            ESP_LOGI(TAG, "home: button %d on %s", b, home_app(sel, extra)->name);
            if (b == BTN_PLUS) sel = (sel + count - 1) % count; // MENU + is the left one: move left
            else if (b == BTN_MINUS) sel = (sel + 1) % count; // MENU - is the right one: move right
            else open = 1;
        }
        if (!open) continue;
        const app_t *a = home_app(sel, extra);
        const app_info_t *info = sel < napps ? app_infos[sel] : NULL;
        ESP_LOGI(TAG, "opening %s", a->name);
        os_app(a, info);
        ui_keys(NULL, NULL, NULL, NULL);
        if (info && info->launch_info) os_launch_info();
        ui_wipe(); // the home/Info screen would ghost through the app's first frame
        ui_close();
        btn_flush();
        a->run();
        os_app(NULL, NULL);
        ui_keys("< Left", "Right >", "Open", "Open");
        usb_host_lib_info_t usb;
        if (usb_host_lib_info(&usb) == ESP_OK) esp_restart(); // the app made USB a host: home must stay flashable
        if (usb_serial_jtag_is_driver_installed()) esp_restart(); // the app took the USB serial port (Codex Board's feed):
                                                                  // a restart hands it back to the console
        snd_stop();
        if (!ranging_running()) wifi_off(), ranging_start(); // the app took the radio: ranging gets it back
        leds((const uint8_t[LEDS][3]){{0}});
        ui_wipe(); // and the app through the home screen
        btn_flush();
        extra_count = refresh_home_apps(extra, &sel);
    }
}
