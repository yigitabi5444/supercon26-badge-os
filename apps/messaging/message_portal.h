#pragma once
#include <stdbool.h>
#include <stdint.h>
// One portal lifetime per boot. Hiding its menu keeps AP/DNS/HTTP alive; global Home releases them.
bool message_portal_start(const uint8_t thread[16]); // Reuse a started session and select this context; NULL = new message.
const char *message_portal_ssid(void);
const char *message_portal_password(void);
void message_portal_selected(uint8_t out[16]);
// Current badge UTC, else the runtime phone/monotonic reference; 0 means unavailable.
uint64_t message_portal_now(void);
