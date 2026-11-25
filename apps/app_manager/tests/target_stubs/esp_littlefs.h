// Narrow call-count test double, NOT SDK/target compilation evidence.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_flash.h"
typedef struct {
    const char *base_path,*partition_label;
    bool format_if_mount_failed,grow_on_mount,read_only;
} esp_vfs_littlefs_conf_t;
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *);
esp_err_t esp_vfs_littlefs_unregister(const char *);
esp_err_t esp_littlefs_info(const char *,size_t *,size_t *);
