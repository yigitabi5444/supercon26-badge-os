#pragma once
#include <stddef.h>
#include "badge_native_api.h"
#include "badge/messaging.h" // Frozen existing value types; no service functions are guest imports.

enum { BADGE_MSG_NO_KEY=-1,BADGE_MSG_PLUS=0,BADGE_MSG_MINUS=1,BADGE_MSG_A=2,BADGE_MSG_B=3,
       BADGE_MSG_LABEL_MAX=20,BADGE_MSG_STATUS_MAX=256 };
typedef struct { char ssid[20],password[17]; } badge_msg_phone_t;
// Messages only: badge_app_get_api(2,sizeof(badge_messages_api_t)).
// Existing message_state_t is copied synchronously; all storage, unread/radio,
// phone AP/HTTP/DNS/page/session state and callbacks remain resident host-owned.
typedef struct {
    badge_native_api_t base;
    int32_t (*start)(void *); // Existing idempotent messaging_start; success1, unavailable0.
    int32_t (*key)(void *,uint32_t); // <=1000ms, physical0..3 or NO_KEY. No lower-button exit chord.
    void (*keys)(void *,const char *,const char *,const char *,const char *); // Frame lease; copies4 labels<=20B before submit.
    int32_t (*snapshot)(void *,message_state_t *,uint64_t *); // Full copied state, optional copied revision; success1.
    int32_t (*read_thread)(void *,const uint8_t *); // Copies exactly16 ID bytes, existing MSG_* result.
    int32_t (*status)(void *,char *,uint32_t); // Capacity1..256, copied/NUL; count or-1 on invalid call.
    uint64_t (*utc_now)(void *); // Existing badge UTC, else runtime authenticated phone/monotonic reference, else0.
    int32_t (*portal_start)(void *,const uint8_t *); // NULL=new or copied16-byte thread; original one portal attempt per boot.
    int32_t (*portal_view)(void *,badge_msg_phone_t *); // Copies host-owned SSID/password; no retained guest output pointer.
    int32_t (*info)(void *); // Existing Messages Info through resident host, drained frame owner.
    int32_t (*release)(void *); // Success1 only before any portal attempt; otherwise0 and retain ELF/copy/lease until Home reset.
} badge_messages_api_t;
_Static_assert(offsetof(badge_messages_api_t,base)==0,"Native prefix offset");
_Static_assert(offsetof(badge_messages_api_t,start)==sizeof(badge_native_api_t),"Native prefix unchanged");
_Static_assert(sizeof(badge_msg_phone_t)==37,"Copied phone view layout");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(badge_messages_api_t)==116,"ESP32-S3 Messages extension layout");
#endif
// Preserve original B meanings and MENU Home reboot. Once phone mode is
// attempted, hiding the pane does not stop its resident services or allow unload.
// Original storage-unavailable B before phone mode may return normally. No fake
// httpd_stop/DNS shutdown, guest callback, generic service access or new protocol.
