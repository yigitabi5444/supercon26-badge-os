// App-private scan cache; included after the shared JSON-string helper.
// Settings owns the radio here: wifi_on stopped ranging. Names stay in RAM, never NVS.
#define NET_MAX 16
#define NET_REPLY_BYTES 4096
#define NET_COOLDOWN_US (30 * 1000000LL)
typedef struct { char ssid[33]; int8_t rssi; } network_t;
static network_t networks[NET_MAX];
static int network_count, network_omitted;
static bool network_busy, network_error;
static bool network_usable = true; // a missing completion cannot contaminate a later scan in this session
static int64_t network_updated, network_attempt;
static SemaphoreHandle_t network_lock, scan_done;
static TaskHandle_t network_task;
static volatile bool scan_failed, scan_abandoned;

// Invalid UTF-8 must not poison JSON. NUL/binary names are outside the existing string settings format.
static bool ssid_text(const uint8_t *s, int n)
{
    for (int i = 0; i < n;) {
        uint32_t c = s[i++], min = 0;
        int more = 0;
        if (c < 0x80) { if (c < 0x20 || c == 0x7f) return false; continue; }
        if (c >= 0xc2 && c <= 0xdf) more = 1, min = 0x80, c &= 0x1f;
        else if (c >= 0xe0 && c <= 0xef) more = 2, min = 0x800, c &= 0x0f;
        else if (c >= 0xf0 && c <= 0xf4) more = 3, min = 0x10000, c &= 7;
        else return false;
        if (i + more > n) return false;
        while (more--) {
            if ((s[i] & 0xc0) != 0x80) return false;
            c = (c << 6) | (s[i++] & 0x3f);
        }
        if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff) || c <= 0x9f) return false;
    }
    return true;
}

static void network_add(network_t *list, int *count, const wifi_ap_record_t *ap, int *omitted)
{
    int len = strnlen((const char *)ap->ssid, 32);
    if (!len) return; // hidden/empty: manual entry remains available
    if (!ssid_text(ap->ssid, len)) { (*omitted)++; return; }
    char name[33]; memcpy(name, ap->ssid, len); name[len] = 0;
    for (int i = 0; i < *count; i++)
        if (!strcmp(list[i].ssid, name)) { if (ap->rssi > list[i].rssi) list[i].rssi = ap->rssi; return; }
    int slot;
    if (*count < NET_MAX) slot = (*count)++;
    else {
        slot = 0;
        for (int i = 1; i < *count; i++) if (list[i].rssi < list[slot].rssi) slot = i;
        if (ap->rssi <= list[slot].rssi) return;
    }
    strcpy(list[slot].ssid, name); list[slot].rssi = ap->rssi;
}

static void network_scan_done(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (scan_abandoned) {
        if (network_task) xTaskNotifyGive(network_task); // worker frees any late driver results; never retry
        else esp_wifi_clear_ap_list(); // initial timeout / worker allocation failure: event-task-safe list cleanup
        return;
    }
    const wifi_event_sta_scan_done_t *done = data;
    scan_failed = !done || done->status != 0;
    xSemaphoreGive(scan_done); // callback never scans, draws, allocates or writes settings
}

static void network_scan(void)
{
    network_t next[NET_MAX];
    int count = 0, omitted = 0;
    bool uncertain = false;
    wifi_country_t country = {0};
    esp_err_t err = esp_wifi_get_country(&country);
    wifi_ap_record_t *records = heap_caps_malloc(NET_MAX * sizeof *records, MALLOC_CAP_SPIRAM);
    if (!records) err = ESP_ERR_NO_MEM;
    if (err == ESP_OK && (!country.schan || !country.nchan || country.schan + country.nchan > 15)) err = ESP_ERR_INVALID_ARG;
    int64_t deadline = esp_timer_get_time() + 6000000;
    for (int channel = country.schan; err == ESP_OK && channel < country.schan + country.nchan; channel++) {
        wifi_scan_config_t scan = {.channel = channel, .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                                  .scan_time.active = {.min = 20, .max = 40}, .home_chan_dwell_time = 80};
        xSemaphoreTake(scan_done, 0); // discard an earlier completion
        scan_failed = false;
        err = esp_wifi_scan_start(&scan, false);
        if (err == ESP_OK && !xSemaphoreTake(scan_done, pdMS_TO_TICKS(400))) {
            err = ESP_ERR_TIMEOUT; uncertain = scan_abandoned = true;
            esp_wifi_scan_stop(); // no retry until Settings restarts: a late completion has no request ID to match
        }
        if (err == ESP_OK && scan_failed) err = ESP_FAIL;
        uint16_t n = NET_MAX;
        if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&n, records); // frees all driver results
        if (err != ESP_OK) { esp_wifi_clear_ap_list(); break; }
        for (int i = 0; i < n && i < NET_MAX; i++) network_add(next, &count, &records[i], &omitted);
        ms(80); // home/AP airtime between short channel scans; never stop/reconfigure the live AP
        if (esp_timer_get_time() >= deadline) err = ESP_ERR_TIMEOUT;
    }
    free(records);
    xSemaphoreTake(network_lock, portMAX_DELAY);
    if (err == ESP_OK) {
        memcpy(networks, next, count * sizeof *next);
        network_count = count; network_omitted = omitted; network_updated = esp_timer_get_time();
    }
    if (uncertain) network_usable = false;
    network_error = err != ESP_OK; network_busy = false; // errors retain the last successful list
    xSemaphoreGive(network_lock);
}

static void network_worker(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (scan_abandoned) esp_wifi_clear_ap_list(); // late completion after timeout cleanup
        else network_scan();
    }
}

static esp_err_t network_reply(httpd_req_t *r, bool refresh)
{
    if (r->content_len > 0) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "No body expected.");
    network_t snapshot[NET_MAX];
    int64_t now = esp_timer_get_time(), updated, attempt;
    int count, omitted; bool busy, error, available, start = false;
    if (network_lock) xSemaphoreTake(network_lock, portMAX_DELAY);
    if (refresh && network_task && network_usable && !network_busy && now - network_attempt >= NET_COOLDOWN_US) {
        network_busy = true; network_attempt = now; start = true;
    }
    count = network_count; omitted = network_omitted; busy = network_busy; error = network_error;
    available = network_task && network_usable;
    updated = network_updated; attempt = network_attempt;
    memcpy(snapshot, networks, count * sizeof *snapshot);
    if (network_lock) xSemaphoreGive(network_lock);
    if (start) xTaskNotifyGive(network_task);
    if (refresh && !start) httpd_resp_set_status(r, available ? "429 Too Many Requests" : "503 Service Unavailable");
    char *out = heap_caps_malloc(NET_REPLY_BYTES, MALLOC_CAP_SPIRAM), *p = out;
    if (!out) return httpd_resp_send_500(r);
    _Static_assert(NET_REPLY_BYTES >= NET_MAX * (32 * 6 + 16) + 256, "network JSON fits");
    p += sprintf(p, "{\"networks\":[");
    for (int i = 0; i < count; i++) {
        if (i) *p++ = ',';
        *p++ = '{'; json_str(&p, "ssid", snapshot[i].ssid); p[-1] = '}';
    }
    int retry = attempt + NET_COOLDOWN_US > now ? (int)((attempt + NET_COOLDOWN_US - now + 999999) / 1000000) : 0;
    p += sprintf(p, "],\"busy\":%s,\"error\":%s,\"available\":%s,\"age\":%d,\"retry_after\":%d,\"omitted\":%d}",
                 busy ? "true" : "false", error ? "true" : "false", available ? "true" : "false", updated ? (int)((now - updated) / 1000000) : 0, retry, omitted);
    httpd_resp_set_type(r, "application/json"); httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_send(r, out, p - out);
    free(out); return result;
}

static esp_err_t get_networks(httpd_req_t *r) { return network_reply(r, false); }
static esp_err_t scan_networks(httpd_req_t *r) { return network_reply(r, true); }
