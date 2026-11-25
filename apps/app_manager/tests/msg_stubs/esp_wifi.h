#pragma once
#include <stdint.h>
#include "esp_http_server.h"
enum {WIFI_AUTH_WPA2_PSK=3,WIFI_MODE_AP=2,WIFI_IF_AP=1};
typedef struct {struct {int channel,max_connection,authmode,ssid_len;uint8_t ssid[32],password[64];} ap;} wifi_config_t;
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int,const wifi_config_t *);
esp_err_t esp_wifi_start(void);
