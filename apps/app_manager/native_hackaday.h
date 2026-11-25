#pragma once
#include "native_host.h"
#include "badge_hackaday_api.h"

// Trusted host implementations only. The network worker owns all job memory;
// these callbacks never retain a guest buffer, URI or function pointer.
typedef struct {
    void *context;
    am_result_t (*begin)(void *);
    void *(*allocate)(void *,uint32_t);
    void (*deallocate)(void *,void *);
    int32_t (*begin_feed)(void *);
    int32_t (*begin_image)(void *,const char *,uint32_t,uint32_t);
    int32_t (*poll)(void *,badge_had_result_t *);
    int32_t (*end_job)(void *,uint32_t);
    am_result_t (*finish)(void *); // AM_BUSY retains job and all host/module leases.
} am_native_hackaday_t;
extern const am_native_hackaday_t am_native_hackaday_target;
am_result_t am_native_host_start_hackaday(const am_native_hackaday_t *);
am_result_t app_manager_launch_hackaday_snapshot(const uint8_t *,size_t,const am_metadata_t *,const am_native_hackaday_t *);
