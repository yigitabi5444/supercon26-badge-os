#pragma once
#include <stddef.h>
#include "badge_native_api.h"

enum { BADGE_CODEX_USB=0, BADGE_CODEX_LAN=1, BADGE_CODEX_INVALID=2,
       BADGE_CODEX_BODY_MAX=8192, BADGE_CODEX_SEEN_MAX=1057,
       BADGE_CODEX_USB_READ_MAX=256, BADGE_CODEX_LABEL_MAX=20 };
enum { BADGE_CODEX_NONE=0, BADGE_CODEX_PLUS=1, BADGE_CODEX_MINUS=2,
       BADGE_CODEX_A=3, BADGE_CODEX_B=4, BADGE_CODEX_EXIT=5 };
enum { BADGE_CODEX_FETCH_INIT=-1, BADGE_CODEX_FETCH_OFFLINE=-2, BADGE_CODEX_FETCH_HTTP=-3,
       BADGE_CODEX_FETCH_TOO_BIG=-4, BADGE_CODEX_FETCH_INCOMPLETE=-5 };
// Codex Board only, through badge_app_get_api(2,sizeof(badge_codex_api_t)).
// Same original72B prefix; no arbitrary endpoints, settings keys, USB writes or SDK imports.
typedef struct {
    badge_native_api_t base;
    uint32_t (*mode)(void *); // Snapshot of trusted cb_url: unset=USB, valid configured http://=LAN.
    uint64_t (*now_us)(void *);
    int32_t (*button)(void *,uint32_t); // <=100ms, one queued button; observed B is normal Home return.
    void (*red_leds)(void *,uint32_t); // Only0/off or1/red level10, all8 LEDs.
    int32_t (*seen_load)(void *,char *,uint32_t); // Only cb_seen; 1..1057B copied/NUL-terminated, success=1.
    int32_t (*seen_save)(void *,const char *); // Only cb_seen, boundedNUL <=1056B; success=1.
    int32_t (*lan_join)(void *); // Saved Wi-Fi, fixed5000ms, configured LAN mode only.
    int32_t (*lan_fetch)(void *,char *,uint32_t); // Exactly8192B capacity; byte count or negative FETCH code.
    int32_t (*usb_begin)(void *); // Resident host driver; original fixed READY line.
    int32_t (*usb_read)(void *,char *,uint32_t); // 1..256B, nonblocking, copied before return.
    int32_t (*usb_ack)(void *,const char *); // Only original ACK line for1..32 hex revision; success=1.
    void (*release)(void *); // Synchronously release app I/O/LEDs; USB ISR/driver stays host-resident.
    void (*keys)(void *,const char *,const char *,const char *,const char *); // Frame lease; copies4 labels<=20B each.
} badge_codex_api_t;
_Static_assert(offsetof(badge_codex_api_t,base)==0,"Native prefix offset");
_Static_assert(offsetof(badge_codex_api_t,mode)==sizeof(badge_native_api_t),"Native prefix unchanged");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(badge_codex_api_t)==124,"ESP32-S3 Codex Board extension layout");
#endif
// Render through base frame/drawing/idle calls; no lower A+B hold exit for Codex Board.
// Feed/USB framing, parsing, stale/paging/alerts and rendering remain in the module.
