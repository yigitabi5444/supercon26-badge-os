#pragma once
#include <stdbool.h>
#include "esp_loader.h"
// Only the Update app calls these; no caller-supplied qualification flag.
bool update_layout_local_legacy(void);
bool update_layout_peer_legacy(esp_loader_t *);
