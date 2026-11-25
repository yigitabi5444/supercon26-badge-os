#pragma once
#include "video_asset.h"
#include "package_http.h"

typedef struct {
    const am_source_config_t *code;
    const char *receipt_url,*signature_url,*clip_url;
} am_video_source_config_t; // Immutable curated binding; all asset URLs share the code origin/TLS policy.
extern const am_video_source_config_t am_video_source_config;
bool am_video_source_valid(const am_video_source_config_t *);
// Called under cancellable Home deferral, before code activation; no whole clip buffer.
am_result_t am_video_source_prepare(am_video_store_t *,const am_video_source_config_t *,am_https_t *,
                                     bool (*)(void *),void *,am_progress_t);
