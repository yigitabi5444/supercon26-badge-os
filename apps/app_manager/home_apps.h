#pragma once
#include <stddef.h>
#include "badge/app.h"
// Optional Home hook. Call after first visible Home/OTA validation and on Home
// re-entry, never from rendering or key polling. Entries are stable resident
// shortcuts; count<=min(capacity,7). Failure/unavailable yields no extras.
size_t app_manager_home_apps(const app_t **out,size_t capacity);
