// Wi-Fi: ranging (ranging.c) keeps the radio busy on every screen; an app that needs it otherwise takes it over with
// wifi_on / wifi_connect, and the home screen gives it back to ranging.
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "badge/badge.h"

static EventGroupHandle_t wifi_ev; // bit 0: got an IP
static volatile bool wifi_join;    // keep (re)joining the badge Wi-Fi

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && wifi_join) xEventGroupSetBits(wifi_ev, 1);
    if ((base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) ||
        (base == WIFI_EVENT && (id == WIFI_EVENT_STA_DISCONNECTED || id == WIFI_EVENT_STA_STOP)))
        xEventGroupClearBits(wifi_ev, 1);
    if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED) && wifi_join)
        esp_wifi_connect();
}

void wifi_on(void)
{
    ranging_stop(); // an app that wants the radio gets it: ranging is back at the next home screen / reboot
    if (!wifi_ev) { // the driver is set up once and kept; on/off only starts and stops the radio
        wifi_ev = xEventGroupCreate();
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        init.nvs_enable = cfg_storage_ready(); // unavailable NVS must not be initialized/erased by Wi-Fi
        ESP_ERROR_CHECK(esp_wifi_init(&init));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL);
        esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL);
    }
    esp_wifi_start(); // already running: harmless error
}

bool wifi_connect(int timeout_ms)
{
    wifi_config_t c = {0};
    char ssid[33]; // NVS strings include NUL; the driver's full 32-byte field need not
    int n = cfg_get("ssid", ssid, sizeof ssid);
    if (n <= 0 || n > (int)sizeof c.sta.ssid) return false;
    memcpy(c.sta.ssid, ssid, n);
    cfg_get("pass", (char *)c.sta.password, sizeof c.sta.password);
    wifi_on();
    esp_wifi_set_config(WIFI_IF_STA, &c);
    wifi_join = true;
    esp_wifi_set_ps(WIFI_PS_NONE); // modem sleep slows downloads a lot (Online update); the radio is busy anyway
    esp_wifi_connect(); // not started yet: harmless error, the STA_START event connects
    return xEventGroupWaitBits(wifi_ev, 1, false, true, pdMS_TO_TICKS(timeout_ms)) & 1;
}

bool wifi_connected(void) { return wifi_ev && (xEventGroupGetBits(wifi_ev) & 1); }

void wifi_off(void)
{
    if (!wifi_ev) return;
    ranging_stop();
    wifi_join = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
    xEventGroupClearBits(wifi_ev, 1);
}
