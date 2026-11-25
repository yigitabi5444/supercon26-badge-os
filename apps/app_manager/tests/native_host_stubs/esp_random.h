#pragma once
#include <stdint.h>
#include <stddef.h>
void esp_fill_random(void *,size_t);
uint32_t esp_random(void);
