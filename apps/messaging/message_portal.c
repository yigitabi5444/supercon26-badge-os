// Reuses Settings' QR + SoftAP + HTTP + captive DNS approach without exposing cfg credentials.
#include "message_portal.h"
#include "badge/messaging.h"
#include "badge/badge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "message_page.h"
#define ADDR "192.168.4.1"
static char ssid[20], password[17], key[33];
static uint8_t selected[16];
static SemaphoreHandle_t selected_lock;
static bool started, attempted;
static void select_thread(const uint8_t id[16])
{ xSemaphoreTake(selected_lock, portMAX_DELAY); memcpy(selected, id, 16); xSemaphoreGive(selected_lock); }
void message_portal_selected(uint8_t out[16])
{ xSemaphoreTake(selected_lock, portMAX_DELAY); memcpy(out, selected, 16); xSemaphoreGive(selected_lock); }
// Runtime-only current phone time; independent of message/retry timestamps and never persisted.
static uint64_t phone_epoch;
static int64_t phone_at;
uint64_t message_portal_now(void)
{
    time_t wall = time(NULL);
    if (wall >= 1577836800LL && wall <= 4102444800LL) return (uint64_t)wall;
    if (!selected_lock) return 0;
    xSemaphoreTake(selected_lock, portMAX_DELAY);
    uint64_t epoch = phone_epoch; int64_t at = phone_at;
    xSemaphoreGive(selected_lock);
    int64_t elapsed = esp_timer_get_time() - at;
    if (!epoch || elapsed < 0) return 0;
    uint64_t now = epoch + (uint64_t)elapsed / 1000000;
    return now <= 4102444800ULL ? now : 0;
}
static void accept_phone_time(httpd_req_t *r)
{
    size_t n = httpd_req_get_hdr_value_len(r, "X-Msg-Time"); char value[11];
    if (!selected_lock || !n || n > 10 || httpd_req_get_hdr_value_str(r, "X-Msg-Time", value, sizeof value) != ESP_OK) return;
    uint64_t seconds = 0;
    for (size_t i = 0; i < n; i++) {
        if (value[i] < '0' || value[i] > '9') return;
        seconds = seconds * 10 + value[i] - '0';
    }
    if (seconds < 1577836800ULL || seconds > 4102444800ULL) return;
    int64_t at = esp_timer_get_time();
    xSemaphoreTake(selected_lock, portMAX_DELAY);
    phone_epoch = seconds; phone_at = at;
    xSemaphoreGive(selected_lock);
}
static esp_err_t redirect(httpd_req_t *r, httpd_err_code_t err)
{
    httpd_resp_set_status(r, "303 See Other"); httpd_resp_set_hdr(r, "Location", "http://" ADDR "/");
    return httpd_resp_sendstr(r, "Badge messages: http://" ADDR "/");
}
static esp_err_t error(httpd_req_t *r, const char *status, const char *body)
{ httpd_resp_set_status(r, status); return httpd_resp_sendstr(r, body); }
static bool authorized(httpd_req_t *r)
{
    char supplied[40];
    return httpd_req_get_hdr_value_len(r, "X-Msg-Key") == 32 &&
        httpd_req_get_hdr_value_str(r, "X-Msg-Key", supplied, sizeof supplied) == ESP_OK && !strcmp(supplied, key);
}
static esp_err_t page(httpd_req_t *r)
{
    char host[64];
    if (httpd_req_get_hdr_value_str(r, "Host", host, sizeof host) != ESP_OK ||
        (strcmp(host, ADDR) && strcmp(host, ADDR ":80"))) return redirect(r, 0);
    httpd_resp_set_type(r, "text/html; charset=utf-8"); httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "Content-Security-Policy", "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; form-action 'none'");
    const char *at = strstr(message_page, "@KEY@");
    if (httpd_resp_send_chunk(r, message_page, at - message_page) != ESP_OK ||
        httpd_resp_send_chunk(r, key, 32) != ESP_OK || httpd_resp_sendstr_chunk(r, at + 5) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(r, NULL, 0);
}
static void js(char **out, const char *s)
{
    char *p = *out; *p++ = '"';
    while (*s) { unsigned char c = *s++; if (c == '\n') { *p++ = '\\'; *p++ = 'n'; }
        else { if (c == '"' || c == '\\') *p++ = '\\'; *p++ = c; } }
    *p++ = '"'; *out = p;
}
static esp_err_t state(httpd_req_t *r)
{
    if (!authorized(r)) return httpd_resp_send_err(r, HTTPD_403_FORBIDDEN, "Reopen the badge page.");
    accept_phone_time(r); // only after authentication; optional for older phone pages
    message_state_t *s = malloc(sizeof *s); char *out = malloc(8192), *p = out;
    if (!s || !out || !messaging_snapshot(s, NULL)) { free(s); free(out); return error(r, "503 Service Unavailable", messaging_status()); }
    uint8_t active[16]; char hex[33]; message_portal_selected(active);
    bool exists = false; for (int i = 0; i < s->count; i++) if (!memcmp(s->messages[i].thread, active, 16)) exists = true;
    uint8_t me[6]; ranging_me(me);
    p += sprintf(p, "{\"limit\":%d,\"device\":\"%02x%02x%02x%02x%02x%02x\",\"relay\":%s,\"selected\":", MSG_CAP, me[0],me[1],me[2],me[3],me[4],me[5], s->relay ? "true" : "false");
    if (exists) { message_hex(active, hex); js(&p, hex); } else p += sprintf(p, "null");
    p += sprintf(p, ",\"messages\":[");
    // Eight bounded ASCII records fit within 8 KB, including escaping.
    for (int i = 0; i < s->count; i++) {
        message_t *m = &s->messages[i];
        p += sprintf(p, "%s{\"id\":", i ? "," : ""); message_hex(m->id, hex); js(&p, hex);
        p += sprintf(p, ",\"thread\":"); message_hex(m->thread, hex); js(&p, hex);
        p += sprintf(p, ",\"sender\":\"%02x%02x%02x%02x%02x%02x\",\"name\":", m->sender[0],m->sender[1],m->sender[2],m->sender[3],m->sender[4],m->sender[5]); js(&p, m->name);
        p += sprintf(p, ",\"title\":"); js(&p, m->title); p += sprintf(p, ",\"body\":"); js(&p, m->body);
        p += sprintf(p, ",\"clock\":%u,\"seconds\":\"%llu\",\"read\":%s}", m->clock, (unsigned long long)m->seconds, s->read[i] ? "true" : "false");
    }
    p += sprintf(p, "]}"); httpd_resp_set_type(r, "application/json"); httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_send(r, out, p - out); free(s); free(out); return result;
}
static esp_err_t action(httpd_req_t *r)
{
    if (!authorized(r)) return httpd_resp_send_err(r, HTTPD_403_FORBIDDEN, "Reopen the badge page.");
    static uint64_t window; static unsigned changes;
    uint64_t now = esp_timer_get_time() / 1000;
    if (now - window >= 1000) { window = now; changes = 0; }
    if (changes++ >= 2) return error(r, "429 Too Many Requests", "Please wait a second, then retry the same action.");
    char b[512];
    if (r->content_len <= 0 || r->content_len > sizeof b) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Request too large.");
    int got = 0;
    while (got < r->content_len) { int n = httpd_req_recv(r, b + got, r->content_len - got); if (n <= 0) return ESP_FAIL; got += n; }
    char *fields[6]; int count = 0, at = 0;
    while (at < got && count < 6) { fields[count++] = b + at; char *end = memchr(b + at, 0, got - at); if (!end) break; at = end - b + 1; }
    uint8_t id[16], thread[16]; int result = MSG_INVALID;
    if (at != got) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Incomplete request.");
    if (count == 6 && !strcmp(fields[0], "send") && message_unhex(fields[1], id) && message_unhex(fields[2], thread)) {
        char *end; bool digits = *fields[5] != 0;
        for (char *c = fields[5]; *c; c++) if (*c < '0' || *c > '9') digits = false;
        if (digits && strlen(fields[5]) <= 10) {
            uint64_t seconds = strtoull(fields[5], &end, 10);
            if (!*end) result = messaging_submit(id, thread, fields[3], fields[4], seconds);
        }
        if (result <= MSG_DUP) select_thread(thread);
    } else if (count == 2 && !strcmp(fields[0], "relay") && (!strcmp(fields[1], "0") || !strcmp(fields[1], "1"))) result = messaging_relay(fields[1][0] == '1');
    else if (count == 2 && message_unhex(fields[1], thread)) {
        if (!strcmp(fields[0], "open")) { result = messaging_read(thread); if (result <= MSG_DUP) select_thread(thread); }
        else if (!strcmp(fields[0], "forget")) { result = messaging_forget(thread); if (result <= MSG_DUP) { memset(id, 0, 16); select_thread(id); } }
    }
    if (result <= MSG_DUP) return httpd_resp_sendstr(r, "Stored on this badge.");
    if (result == MSG_FULL) return error(r, "409 Conflict", "Message storage is full (8 total max; space is reserved for Settings). Delete a local thread; relayed copies may return.");
    if (result == MSG_IO) return error(r, "503 Service Unavailable", messaging_status());
    if (result == MSG_DELETED) return error(r, "409 Conflict", "This message was deleted here. Discard the old retry draft and compose a new message.");
    if (result == MSG_CONFLICT) return error(r, "409 Conflict", "Message ID already exists with different content. Refresh before editing.");
    return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Invalid message: plain text only, subject <=32, body 1-240, valid IDs/time.");
}
static void dns(void *unused)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = {.sin_family=AF_INET,.sin_port=htons(53),.sin_addr.s_addr=htonl(INADDR_ANY)};
    if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) < 0) { if (s >= 0) close(s); vTaskDelete(NULL); return; }
    for (;;) {
        uint8_t b[512]; struct sockaddr_in from; socklen_t fl = sizeof from;
        int n = recvfrom(s, b, sizeof b - 16, 0, (struct sockaddr *)&from, &fl);
        if (n < 17 || b[4] || b[5] != 1 || (b[2] & 0x80)) continue;
        int p = 12;
        while (p < n && b[p] && b[p] <= 63 && p + 1 + b[p] < n) p += b[p] + 1;
        if (p + 5 > n || b[p]) continue;
        p += 5;
        bool a_query = b[p-4] == 0 && b[p-3] == 1 && b[p-2] == 0 && b[p-1] == 1;
        b[2]=0x81; b[3]=0x80; b[6]=0; b[7]=a_query; memset(b+8, 0, 4);
        const uint8_t answer[] = {0xc0,12,0,1,0,1,0,0,0,30,0,4,192,168,4,1};
        if (a_query) { memcpy(b+p, answer, sizeof answer); p += sizeof answer; }
        sendto(s, b, p, 0, (struct sockaddr *)&from, fl);
    }
}
bool message_portal_start(const uint8_t thread[16])
{
    uint8_t zero[16] = {0};
    if (started) { select_thread(thread ? thread : zero); return true; }
    if (attempted) return false; // failed partial setup is retried only after Home/reboot
    attempted = true;
    selected_lock = xSemaphoreCreateMutex(); if (!selected_lock) return false;
    uint8_t random[16]; select_thread(thread ? thread : zero);
    esp_fill_random(random, 16); message_hex(random, key);
    esp_fill_random(random, 16); char pass[33]; message_hex(random, pass); memcpy(password, pass, 16);
    char id[5]; badge_id(id); snprintf(ssid, sizeof ssid, "Msg-%s", id);
    messaging_transport(NULL); wifi_on(); esp_wifi_stop();
    if (!esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") && !esp_netif_create_default_wifi_ap()) return false;
    wifi_config_t c = {.ap={.channel=1,.max_connection=2,.authmode=WIFI_AUTH_WPA2_PSK}};
    c.ap.ssid_len = snprintf((char *)c.ap.ssid, sizeof c.ap.ssid, "%s", ssid);
    memcpy(c.ap.password, password, 17);
    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK || esp_wifi_set_config(WIFI_IF_AP, &c) != ESP_OK || esp_wifi_start() != ESP_OK) return false;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG(); config.stack_size = 6144; config.lru_purge_enable = true; config.recv_wait_timeout = 3;
    httpd_handle_t server;
    if (httpd_start(&server, &config) != ESP_OK) return false;
    const httpd_uri_t routes[] = {{.uri="/",.method=HTTP_GET,.handler=page}, {.uri="/api/state",.method=HTTP_GET,.handler=state}, {.uri="/api/action",.method=HTTP_POST,.handler=action}};
    for (unsigned i = 0; i < sizeof routes/sizeof *routes; i++) if (httpd_register_uri_handler(server, &routes[i]) != ESP_OK) return false;
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, redirect);
    if (xTaskCreate(dns, "msg-dns", 3072, NULL, 2, NULL) != pdPASS) return false;
    started = true; return true;
}
const char *message_portal_ssid(void) { return ssid; }
const char *message_portal_password(void) { return password; }
