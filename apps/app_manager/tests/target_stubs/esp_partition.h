#pragma once
#include <stdbool.h>
#include "esp_flash.h"
enum { ESP_PARTITION_TYPE_ANY=255,ESP_PARTITION_SUBTYPE_ANY=255 };
typedef struct {
    esp_flash_t *flash_chip;uint8_t type,subtype;uint32_t address,size,erase_size;
    char label[17];bool encrypted,readonly;
} esp_partition_t;
typedef struct iterator *esp_partition_iterator_t;
esp_err_t esp_partition_find_err(int,int,const char *,esp_partition_iterator_t *);
const esp_partition_t *esp_partition_get(esp_partition_iterator_t);
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t);
void esp_partition_iterator_release(esp_partition_iterator_t);
