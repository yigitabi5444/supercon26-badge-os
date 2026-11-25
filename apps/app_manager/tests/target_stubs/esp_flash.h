#pragma once
#include <stdint.h>
typedef int esp_err_t;
enum { ESP_OK=0,ESP_FAIL=-1 };
typedef struct { int unused; } esp_flash_t;
extern esp_flash_t *esp_flash_default_chip;
esp_err_t esp_flash_get_size(esp_flash_t *,uint32_t *);
esp_err_t esp_flash_get_physical_size(esp_flash_t *,uint32_t *);
esp_err_t esp_flash_read(esp_flash_t *,void *,uint32_t,uint32_t);
