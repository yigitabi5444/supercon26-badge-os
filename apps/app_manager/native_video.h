#pragma once
#include "native_host.h"
#include "video_asset.h"

// Trusted host-owned bindings only; never module callbacks. Asset borrows the execution lease.
typedef struct {
    am_video_asset_t asset;
    int32_t (*inflate)(uint8_t *,uint32_t,const uint8_t *,uint32_t);
    bool (*info)(void); // Frame mutex held, no pending drawer; restores body before return.
} am_native_video_t;
am_result_t am_native_host_start_video(const am_native_video_t *);
int32_t am_native_video_inflate(uint8_t *,uint32_t,const uint8_t *,uint32_t);
bool am_native_video_info(void);
am_result_t app_manager_launch_video_snapshot(const uint8_t *,size_t,const am_metadata_t *,const am_native_video_t *);
