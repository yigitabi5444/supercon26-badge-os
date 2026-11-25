#pragma once
#include "native_host.h"
#include "badge_codex_api.h"

// Trusted host implementation only. Synchronous calls never retain module buffers/callbacks.
typedef struct {
    void *context;
    am_result_t (*begin)(void *);
    uint32_t (*mode)(void *);
    void (*red_leds)(void *,uint32_t);
    int32_t (*seen_load)(void *,char *,uint32_t);
    int32_t (*seen_save)(void *,const char *);
    int32_t (*lan_join)(void *);
    int32_t (*lan_fetch)(void *,char *,uint32_t);
    int32_t (*usb_begin)(void *);
    int32_t (*usb_read)(void *,char *,uint32_t);
    int32_t (*usb_ack)(void *,const char *);
    void (*release)(void *);
} am_native_codex_t;
extern const am_native_codex_t am_native_codex_target;
am_result_t am_native_host_start_codex(const am_native_codex_t *);
am_result_t app_manager_launch_codex_snapshot(const uint8_t *,size_t,const am_metadata_t *,const am_native_codex_t *);
