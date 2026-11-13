// Settings: the badge opens a Wi-Fi access point "Badge-XXXX" (password "supercon") with a captive portal, so a
// phone that joins opens the settings page by itself. Saving stores the values (cfg_set) and restarts the badge.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "qrcode.h"
#include "badge/badge.h"
#include "badge/font8x8_basic.h"

#define TAG "settings"
#define PASS "supercon"
#define ADDR "192.168.4.1" // the default SoftAP netif

// Keep the split-field bounds aligned with badge.c and the preview.
#define SUBJECT_MAX 256
#define BODY_MAX 1000
#define LEGACY_DISPLAY_MAX 255 // older badge_text renderer buffer
#include "settings_page.h"

static volatile bool saved;
static const char *const keys[] = {"name", "ssid", "pass", "badge_subject", "badge_body", "badge_text", "wx_city", "wx_lat", "wx_lon", "wx_unit", "cb_url", "tz", "snd_sao"};

static void json_str(char **p, const char *key, const char *v) // "key":"v", (v JSON-escaped)
{
    *p += sprintf(*p, "\"%s\":\"", key);
    for (; *v; v++) {
        unsigned char c = *v;
        if (c == '"' || c == '\\') *(*p)++ = '\\', *(*p)++ = c;
        else if (c < 0x20) *p += sprintf(*p, "\\u%04x", c);
        else *(*p)++ = c;
    }
    *p += sprintf(*p, "\",");
}

#include "settings_networks.h"

static esp_err_t get_page(httpd_req_t *r) { return httpd_resp_send(r, page, sizeof page - 1); }

static esp_err_t get_font(httpd_req_t *r)
{
    httpd_resp_set_type(r, "application/octet-stream");
    return httpd_resp_send(r, (const char *)font8x8_basic, sizeof font8x8_basic);
}

static esp_err_t get_cfg(httpd_req_t *r)
{
    char *out = heap_caps_malloc(16 * 1024 + IMG_BYTES, MALLOC_CAP_SPIRAM), *p = out, v[1024];
    if (!out) return httpd_resp_send_500(r);
    bool img = cfg_get_image((uint8_t *)out); // reuse the picture scratch for JSON after checking presence
    _Static_assert(16 * 1024 + IMG_BYTES >= (sizeof keys / sizeof *keys) * (6 * (sizeof v - 1) + 32) + 32, "JSON fits scratch");
    // Compare the last compatibility mirror before reusing out for JSON. Reads never migrate or erase NVS.
    bool mirrored = cfg_get("badge_legacy", out, BODY_MAX + 1) >= 0;
    cfg_get("badge_text", v, sizeof v);
    bool old_edit = mirrored && strcmp(out, v);
    *p++ = '{';
    for (int i = 0; i < sizeof keys / sizeof *keys; i++) {
        cfg_get(keys[i], v, sizeof v);
        if (old_edit && (!strcmp(keys[i], "badge_subject") || !strcmp(keys[i], "badge_body"))) *v = 0;
        json_str(&p, keys[i], v);
    }
    char id[5];
    badge_id(id);
    p += sprintf(p, "\"id\":\"%s\",\"img\":%s}", id, img ? "true" : "false");
    httpd_resp_set_type(r, "application/json");
    esp_err_t e = httpd_resp_send(r, out, p - out);
    free(out);
    return e;
}

static int recv_all(httpd_req_t *r, char *buf, int n)
{
    for (int got = 0; got < n;) {
        int k = httpd_req_recv(r, buf + got, n - got);
        if (k <= 0) return -1;
        got += k;
    }
    return n;
}

static bool printable(const char *s)
{
    for (; *s; s++)
        if (*s < 0x20 || *s > 0x7e) return false;
    return true;
}

static bool badge_text_valid(const char *s)
{
    for (; *s; s++)
        if ((*s < 0x20 || *s > 0x7e) && *s != '\n' && *s != '\r' && *s != '\t') return false;
    return true;
}

static bool known(const char *k)
{
    for (int i = 0; i < sizeof keys / sizeof *keys; i++)
        if (!strcmp(k, keys[i])) return true;
    return false;
}

static bool coord(const char *s, double max) // empty, or a number in -max..max
{
    char *e;
    double d = strtod(s, &e);
    return !*s || (e != s && !*e && d >= -max && d <= max);
}

// v0.8 keeps reading badge_text. Keep a bounded compatibility view as well as the full new fields;
// Include the subject when both fit its 255-character renderer; otherwise keep the complete body.
// The snapshot lets a later upgrade detect an edit/clear made by v0.8 without a destructive migration.
static bool save_badge_legacy(const char *subject, const char *body)
{
    char legacy[BODY_MAX + 1];
    if (*subject && *body && strlen(subject) + strlen(body) + 1 <= LEGACY_DISPLAY_MAX)
        snprintf(legacy, sizeof legacy, "%s\n%s", subject, body);
    else snprintf(legacy, sizeof legacy, "%s", *body ? body : subject);
    return cfg_set("badge_text", *legacy ? legacy : NULL) && cfg_set("badge_legacy", legacy);
}

static bool feed_url(const char *s) // empty, or http://host... with no spaces, credentials or fragment
{
    return !*s || (strlen(s) <= 240 && !strncmp(s, "http://", 7) && s[7] && s[7] != '/' && printable(s) &&
                   !strpbrk(s, " @#"));
}

// Body: key \0 value \0 ... (no URL decoding needed). Everything is checked before anything is stored.
static esp_err_t post_save(httpd_req_t *r)
{
    static char b[4097];
    if (r->content_len <= 0 || r->content_len >= sizeof b || recv_all(r, b, r->content_len) < 0)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Too long.");
    b[r->content_len] = 0;
    const char *kv[32];
    int n = 0;
    char *p = b, *end = b + r->content_len;
    while (p < end && n < 32) {
        char *zero = memchr(p, 0, end - p);
        if (!zero) break;
        kv[n++] = p;
        p = zero + 1;
    }
    const char *bad = p != end || n % 2 ? "Bad request." : NULL;
    bool subject = false, body = false, legacy = false;
    const char *new_subject = NULL, *new_body = NULL;
    for (int i = 0; i + 1 < n && !bad; i += 2) {
        const char *k = kv[i], *v = kv[i + 1];
        int l = strlen(v);
        bool is_subject = !strcmp(k, "badge_subject"), is_body = !strcmp(k, "badge_body"), is_legacy = !strcmp(k, "badge_text");
        subject |= is_subject;
        body |= is_body;
        legacy |= is_legacy;
        if (is_subject) new_subject = v;
        if (is_body) new_body = v;
        if (!strcmp(k, "name") && (l > 12 || !printable(v))) bad = "Username: up to 12 plain characters (A-Z, 0-9, ...).";
        else if (!strcmp(k, "ssid") && l > 32) bad = "SSID: up to 32 bytes.";
        else if (!strcmp(k, "pass") && l && (l < 8 || l > 63)) bad = "Wi-Fi password: 8 to 63 characters, or empty.";
        else if ((is_subject || is_body || is_legacy) &&
                 (l > (is_subject ? SUBJECT_MAX : BODY_MAX) || !badge_text_valid(v)))
            bad = "Badge text: plain characters and line breaks; subject up to 256, body up to 1000.";
        else if (!strcmp(k, "wx_city") && (l > 64 || !printable(v))) bad = "Place name: up to 64 plain characters.";
        else if (!strcmp(k, "wx_unit") && strcmp(v, "") && strcmp(v, "C") && strcmp(v, "F")) bad = "Unit: C or F.";
        else if (!strcmp(k, "wx_lat") && !coord(v, 90)) bad = "Latitude: a number from -90 to 90.";
        else if (!strcmp(k, "wx_lon") && !coord(v, 180)) bad = "Longitude: a number from -180 to 180.";
        else if (!strcmp(k, "tz") && (l > 63 || !printable(v))) bad = "Time zone: up to 63 plain characters.";
        else if (!strcmp(k, "snd_sao") && l && (l > 1 || *v < '0' || *v > '3')) bad = "Speaker port: 0 to 3.";
        else if (!strcmp(k, "cb_url") && !feed_url(v)) bad = "Feed URL: http:// address, up to 240 characters, no credentials.";
        else if (!known(k)) bad = "Unknown setting.";
    }
    if (subject != body || (legacy && subject)) bad = "Send subject and body together, or legacy badge_text.";
    if (bad) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, bad);
    for (int i = 0; i + 1 < n; i += 2) {
        if (!cfg_set(kv[i], *kv[i + 1] ? kv[i + 1] : NULL)) return httpd_resp_send_500(r); // empty = unset (ssid/pass: back to the event Wi-Fi)
        if (!strcmp(kv[i], "wx_city")) cfg_set("wx_lat", NULL), cfg_set("wx_lon", NULL); // a place name replaces
        if (!strcmp(kv[i], "wx_lat")) cfg_set("wx_city", NULL);                         // coordinates, and back
        if (!strcmp(kv[i], "wx_city") || !strcmp(kv[i], "wx_lat")) cfg_set("wx_place", NULL); // Weather looks it up again
    }
    // Switching to text clears the picture once; clearing both fields also clears the legacy view.
    if (subject && (!save_badge_legacy(new_subject, new_body) || !cfg_set_image(NULL))) return httpd_resp_send_500(r);
    if (legacy && (!cfg_set("badge_subject", NULL) || !cfg_set("badge_body", NULL) ||
                   !cfg_set("badge_legacy", NULL) || !cfg_set_image(NULL)))
        return httpd_resp_send_500(r);
    ESP_LOGI(TAG, "saved %d settings", n / 2);
    httpd_resp_sendstr(r, "Saved. The badge restarts now.");
    saved = true;
    return ESP_OK;
}

static esp_err_t post_image(httpd_req_t *r)
{
    if (r->content_len != IMG_BYTES) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Want 74880 bytes.");
    uint8_t *img = heap_caps_malloc(IMG_BYTES, MALLOC_CAP_SPIRAM);
    bool ok = img && recv_all(r, (char *)img, IMG_BYTES) > 0 && cfg_set_image(img);
    free(img);
    return ok ? httpd_resp_sendstr(r, "ok") : httpd_resp_send_500(r);
}

// Captive portal, part 1: every DNS A query gets the badge's address, so the phone's connectivity check lands here.
static void dns_task(void *arg)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY)};
    bind(s, (struct sockaddr *)&a, sizeof a);
    static const uint8_t answer[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1}; // name ptr, A, IN, TTL 60
    for (;;) {
        uint8_t b[512];
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        int n = recvfrom(s, b, sizeof b - sizeof answer, 0, (struct sockaddr *)&from, &fl);
        int p = 12; // skip the header, then the first question's name
        while (p < n && b[p]) p += b[p] + 1;
        p += 5; // name end, QTYPE, QCLASS
        if (n < 12 || p > n) continue;
        bool a_query = b[p - 4] == 0 && b[p - 3] == 1;
        b[2] = 0x81, b[3] = 0x80;                     // response, no error
        b[4] = 0, b[5] = 1, b[6] = 0, b[7] = a_query; // 1 question, 1 answer for A (others: none)
        memset(b + 8, 0, 4);                          // drop authority/additional (EDNS)
        if (a_query) memcpy(b + p, answer, sizeof answer), p += sizeof answer;
        sendto(s, b, p, 0, (struct sockaddr *)&from, fl);
    }
}

// Captive portal, part 2: anything unknown (generate_204, hotspot-detect.html, ...) goes to the page.
// iOS wants a body, not only the redirect.
static esp_err_t to_portal(httpd_req_t *r, httpd_err_code_t err)
{
    httpd_resp_set_status(r, "303 See Other");
    httpd_resp_set_hdr(r, "Location", "http://" ADDR "/");
    return httpd_resp_sendstr(r, "Badge settings: http://" ADDR "/");
}

static void draw_qr(esp_qrcode_handle_t qr) // right half, as big as fits in 336 px
{
    int n = esp_qrcode_get_size(qr), m = 336 / n, x0 = 576 + (336 - n * m) / 2, y0 = TOP + 120;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            if (esp_qrcode_get_module(qr, x, y)) box(x0 + x * m, y0 + y * m, m, m, true);
}

static void run(void)
{
    if (!cfg_storage_ready()) {
        ui_keys("Info", NULL, NULL, "Home"); memset(fb, 0, PLANE);
        text(40, TOP + 48, 3, "Saved settings need recovery.", true);
        text(40, TOP + 112, 2, "Existing data was preserved, not erased.", true);
        text(40, TOP + 152, 2, "Back up NVS before any recovery/reset.", true);
        ui_frame(1); ui_close();
        for (;;) { int b = btn_get(-1); if (b == BTN_PLUS) ui_info(); else if (b == BTN_B) return; }
    }
    char id[5], ssid[16];
    badge_id(id);
    snprintf(ssid, sizeof ssid, "Badge-%s", id);

    wifi_on(); // stops ranging; STA mode, driver up
    network_lock = xSemaphoreCreateMutex();
    scan_done = xSemaphoreCreateBinary();
    network_error = true;
    bool scanner = network_lock && scan_done &&
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, network_scan_done, NULL) == ESP_OK;
    if (scanner) {
        network_busy = true; network_attempt = esp_timer_get_time();
        network_scan(); // populate cache before any tablet can join the portal
    }
    esp_wifi_stop();
    esp_netif_create_default_wifi_ap(); // once: going home restarts the badge
    wifi_config_t c = {.ap = {.password = PASS, .channel = 1, .max_connection = 4, .authmode = WIFI_AUTH_WPA2_PSK}};
    c.ap.ssid_len = snprintf((char *)c.ap.ssid, sizeof c.ap.ssid, "%s", ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA)); // unjoined STA for scanning
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &c));
    ESP_ERROR_CHECK(esp_wifi_start());
    if (scanner && xTaskCreate(network_worker, "settings scan", 4096, NULL, 4, &network_task) != pdPASS) {
        network_task = NULL; network_error = true;
    }
    if (scan_abandoned && network_task) xTaskNotifyGive(network_task); // covers a late event before worker creation
    xTaskCreate(dns_task, "dns", 3072, NULL, 4, NULL);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.lru_purge_enable = true;
    httpd_handle_t srv;
    ESP_ERROR_CHECK(httpd_start(&srv, &cfg));
    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = get_page},
        {.uri = "/badge-font", .method = HTTP_GET, .handler = get_font},
        {.uri = "/cfg", .method = HTTP_GET, .handler = get_cfg},
        {.uri = "/networks", .method = HTTP_GET, .handler = get_networks},
        {.uri = "/networks/scan", .method = HTTP_POST, .handler = scan_networks},
        {.uri = "/save", .method = HTTP_POST, .handler = post_save},
        {.uri = "/image", .method = HTTP_POST, .handler = post_image},
    };
    for (int i = 0; i < sizeof uris / sizeof *uris; i++) httpd_register_uri_handler(srv, &uris[i]);
    httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, to_portal);
    ESP_LOGI(TAG, "SoftAP \"%s\", password \"" PASS "\", http://" ADDR "/", ssid);

    ui_keys("Info", NULL, NULL, "Home");
    memset(fb, 0, sizeof fb);
    text(48, TOP + 32, 5, "Settings", true);
    text(48, TOP + 120, 3, "Join this Wi-Fi:", true);
    text(48, TOP + 160, 5, ssid, true);
    text(48, TOP + 232, 3, "Password:", true);
    text(48, TOP + 272, 5, PASS, true);
    text(48, TOP + 344, 3, "or scan the QR code.", true);
    text(48, TOP + 416, 3, "The page opens by", true);
    text(48, TOP + 448, 3, "itself, or browse to", true);
    text(48, TOP + 488, 3, "http://" ADDR, true);
    text(48, TOP + 560, 3, "Saving restarts it.", true);
    char wifi_qr[64];
    snprintf(wifi_qr, sizeof wifi_qr, "WIFI:T:WPA;S:%s;P:" PASS ";;", ssid);
    esp_qrcode_config_t q = ESP_QRCODE_CONFIG_DEFAULT();
    q.display_func = draw_qr;
    q.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
    esp_qrcode_generate(&q, wifi_qr);
    ui_frame(1);
    ui_close();

    // The web server runs on its own task: this one only waits for Home or a save, the network never waits on it.
    while (!saved) {
        int b = btn_get(200);
        if (b == BTN_B) esp_restart();
        if (b == BTN_PLUS) ui_info_until(&saved); // phone Save still dismisses Info and restarts
    }
    text(48, TOP + 600, 3, "Saved. Restarting...", true); // a fresh line: no text over erased text
    ui_frame(1);
    esp_restart();
}

static void icon(int x, int y) // a gear
{
    icon_base(x, y);
    int c = ICON / 2;
    for (int r = 0; r < ICON; r++)
        for (int k = 0; k < ICON; k++) {
            int dx = k - c, dy = r - c, d2 = dx * dx + dy * dy;
            bool tooth = d2 < 64 * 64 && (abs(dx) < 12 || abs(dy) < 12 || abs(dx - dy) < 16 || abs(dx + dy) < 16);
            if ((d2 < 46 * 46 || tooth) && d2 > 20 * 20) box(x + k, y + r, 1, 1, false);
        }
}

const app_t app_settings = {"Settings", icon, run};
const app_info_t app_settings_info = {
    "Join the badge hotspot from your phone to edit your name, Wi-Fi, Badge, weather, clock and sound settings.",
    "Code: apps/settings/settings.c", false};
