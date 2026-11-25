#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    const char *url,*body,*usb_input;
    size_t offset,usb_offset,read_limit;
    int status;int64_t declared;
    unsigned reads,clients,opened,cleaned,joins,off,ranging,readies,acks,installs,led_on,saves;
    unsigned read_delay_ms;
    bool init_fail,open_fail,join_fail,incomplete,complete_on_headers,radio,lit,installed;
    char seen[1057];
} cb_sdk_fixture_t;
extern cb_sdk_fixture_t cb_sdk;
void cb_sdk_reset(void);
