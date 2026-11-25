#pragma once
#include "native_host.h"
#include "badge_messages_api.h"
// Trusted resident services only. All calls copy guest inputs synchronously.
typedef struct {
    void *context;
    am_result_t (*begin)(void *);
    int32_t (*start)(void *);
    int32_t (*snapshot)(void *,message_state_t *,uint64_t *);
    int32_t (*read_thread)(void *,const uint8_t *);
    int32_t (*status)(void *,char *,uint32_t);
    uint64_t (*utc_now)(void *);
    int32_t (*portal_start)(void *,const uint8_t *);
    int32_t (*portal_view)(void *,badge_msg_phone_t *);
    int32_t (*info)(void *);
    am_result_t (*finish)(void *); // Always AM_BUSY after any portal attempt until Home reboot.
} am_native_messages_t;
extern const am_native_messages_t am_native_messages_target;
am_result_t am_native_host_start_messages(const am_native_messages_t *);
am_result_t app_manager_launch_messages_snapshot(const uint8_t *,size_t,const am_metadata_t *,const am_native_messages_t *);
