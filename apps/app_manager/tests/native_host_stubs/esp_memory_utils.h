#pragma once
#include <stdbool.h>
bool esp_ptr_byte_accessible(const void *);
bool esp_ptr_in_dram(const void *);
bool esp_ptr_external_ram(const void *);
bool esp_ptr_in_drom(const void *);
