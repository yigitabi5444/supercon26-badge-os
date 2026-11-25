#pragma once
#include <stdint.h>
typedef int esp_err_t;
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_OK 0
#define ESP_ERR_TIMEOUT -2
typedef struct { const char *server; } esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(host) { .server=(host) }
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *);
esp_err_t esp_netif_sntp_sync_wait(TickType_t);
void esp_netif_sntp_deinit(void);
