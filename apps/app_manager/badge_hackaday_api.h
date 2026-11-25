#pragma once
#include <stddef.h>
#include "badge_native_api.h"

enum { BADGE_HAD_PENDING=0, BADGE_HAD_OK=1, BADGE_HAD_FAILED=2,
       BADGE_HAD_EXIT=-2, BADGE_HAD_NO_KEY=-1, BADGE_HAD_PLUS=0,
       BADGE_HAD_MINUS=1, BADGE_HAD_A=2, BADGE_HAD_B=3 };
enum { BADGE_HAD_FEED_MAX=1024*1024, BADGE_HAD_TRANSFER_MAX=768*1024,
       BADGE_HAD_URL_MAX=399, BADGE_HAD_LABEL_MAX=20,
       BADGE_HAD_HEAP_MAX=4*1024*1024, BADGE_HAD_ALLOCATIONS_MAX=80,
       BADGE_HAD_IMAGE_WIDTH_MAX=960, BADGE_HAD_IMAGE_HEIGHT_MAX=544 };
typedef struct {
    uint32_t wifi_joined,join_ms,bytes,length,width,height;
    const uint8_t *data; // Borrowed terminal result; copy retained content before end_job.
    char error[64]; // Always NUL terminated.
} badge_had_result_t;
// Hackaday only: badge_app_get_api(2,sizeof(badge_hackaday_api_t)).
// No guest worker/callback, endpoint for RSS, SDK, filesystem or raw network access.
typedef struct {
    badge_native_api_t base;
    int32_t (*key)(void *,uint32_t); // <=500ms. B retains Back/Refresh/Retry; A+B held600ms returns EXIT.
    void (*keys)(void *,const char *,const char *,const char *,const char *); // Copies4 labels<=20B, applied under next frame lease.
    void *(*alloc)(void *,uint32_t); // Zeroed scoped PSRAM; max1MiB each, 4MiB/80 live allocations total.
    void (*dealloc)(void *,void *); // NULL or exact live allocation only; no worker can use this memory.
    int32_t (*begin_feed)(void *); // Fixed existing https://hackaday.com/blog/feed/; one host-owned job.
    int32_t (*begin_image)(void *,const char *,uint32_t,uint32_t); // Copies validated URL<=399B and box<=960x544; one job.
    int32_t (*poll)(void *,badge_had_result_t *); // Copies bounded status, feed+NUL<1MiB or exact packed mono image.
    int32_t (*end_job)(void *,uint32_t); // close_link0/1. Cancel+join/drain, success1; on0 retain job/lease and request exit.
    void (*blit_mono)(void *,int32_t,int32_t,uint32_t,uint32_t,const uint8_t *,uint32_t); // Frame lease, scoped allocation, packed MSB rows, inside body.
    int32_t (*release)(void *); // After reader frees its allocations and frame_idle. Success only after all jobs drain; no callbacks afterwards.
} badge_hackaday_api_t;
_Static_assert(offsetof(badge_hackaday_api_t,base)==0,"Native prefix offset");
_Static_assert(offsetof(badge_hackaday_api_t,key)==sizeof(badge_native_api_t),"Native prefix unchanged");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(badge_hackaday_api_t)==112,"ESP32-S3 Hackaday extension layout");
_Static_assert(sizeof(badge_had_result_t)==92,"ESP32-S3 Hackaday result layout");
#endif
// Original parser/controller/priority/layout remain in module. Only one job can
// borrow host output, never module buffers. Busy teardown keeps ELF/lease alive;
// forced cleanup cannot invent explicit release or ordinary exit evidence.
