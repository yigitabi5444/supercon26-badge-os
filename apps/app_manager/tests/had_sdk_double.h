#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
typedef struct {
    const uint8_t *body;size_t length,offset,chunk;
    const uint8_t *image;size_t image_length;
    const char *encoding,*location;int status;int64_t declared;
    unsigned opens,cleans,connects,resolves,radio_off,ranging,joins,syncs,reads;
    bool radio,incomplete,init_fail,join_fail,time_fail,private_dns;
    atomic_bool blocked,reading;
    atomic_uint allocations,frees;
} had_sdk_fixture_t;
extern had_sdk_fixture_t had_sdk;
void had_sdk_reset(void);
