#pragma once
#include <stdbool.h>
#include "badge/messaging.h"
typedef struct {
    bool available,fail_server;unsigned reader_keys,starts,reads,writes,servers,routes,randoms,detaches,wifi;
} messages_sdk_t;
extern messages_sdk_t messages_sdk;
void messages_sdk_init(const char *,const char *mode);
void messages_sdk_phone_check(void);
void messages_test_home(void);
