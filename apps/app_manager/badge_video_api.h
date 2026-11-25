#pragma once
#include <stddef.h>
#include "badge_native_api.h"

enum { BADGE_VIDEO_CLIP=0, BADGE_VIDEO_CLIP_BYTES=819646,
       BADGE_VIDEO_WIDTH=832, BADGE_VIDEO_HEIGHT=624,
       BADGE_VIDEO_COMPRESSED_MAX=18991, BADGE_VIDEO_PIXELS=64896 };
// Video-only extension on the same getter/ABI2 prefix. No paths, raw framebuffer or SDK imports.
// Request badge_app_get_api(BADGE_NATIVE_ABI,sizeof(badge_video_api_t)); NULL means unavailable.
typedef struct {
    badge_native_api_t base; // Byte-for-byte original72B target prefix; existing clients unchanged.
    uint64_t (*now_us)(void *);
    void (*wait_us)(void *,uint32_t); // <=3000000us, same immediate MENU Home behavior.
    uint32_t (*asset_size)(void *,uint32_t); // Only BADGE_VIDEO_CLIP; zero when unavailable.
    int32_t (*asset_read)(void *,uint32_t,uint32_t,uint8_t *,uint32_t); // index,offset,out,1..4096B; exact=1.
    int32_t (*inflate)(void *,uint8_t *,uint32_t,const uint8_t *,uint32_t); // 64896B out, <=18991B in; size or -1.
    int32_t (*blit)(void *,int32_t,int32_t,uint32_t,uint32_t,const uint8_t *,uint32_t); // Fixed centred832x624,64896B; frame lease required.
    int32_t (*info)(void *); // No frame lease: drain, show Video info, restore exact paused frame; shown=1, unavailable=0.
} badge_video_api_t;
_Static_assert(offsetof(badge_video_api_t,base)==0,"Native prefix offset");
_Static_assert(offsetof(badge_video_api_t,now_us)==sizeof(badge_native_api_t),"Native prefix unchanged");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(badge_video_api_t)==100,"ESP32-S3 Video extension layout");
#endif
// Use base.input: PLUS Info, A Restart, observed B sets exit_requested for normal return.
// The Video host never synthesizes the SpaceInv lower A+B hold exit.
