// Reuse PR #6's portal with private names/state; no guest callbacks enter SDK services.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#define message_portal_start am_msg_portal_start
#define message_portal_ssid am_msg_portal_ssid
#define message_portal_password am_msg_portal_password
#define message_portal_selected am_msg_portal_selected
#define message_portal_now am_msg_portal_now
#include "../messaging/message_portal.c"
bool am_msg_portal_attempted(void) { return attempted; }
