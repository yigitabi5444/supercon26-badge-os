#pragma once
#include <stdbool.h>
#include <stdint.h>
// Namespaced original resident portal; one lifetime per boot, never guest callbacks.
bool am_msg_portal_start(const uint8_t *);
const char *am_msg_portal_ssid(void);
const char *am_msg_portal_password(void);
uint64_t am_msg_portal_now(void);
bool am_msg_portal_attempted(void);
