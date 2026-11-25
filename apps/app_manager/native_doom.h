#pragma once
#include "badge_doom_api.h"
#include "native_host.h"
typedef struct {
    void *context;am_store_t *store;const am_lease_t *lease;
    int32_t (*begin)(void *,badge_doom_lease_t *);
    void *(*resize)(void *,size_t);void (*free)(void *);
    void (*finish)(void *);
} am_native_doom_t;
am_result_t am_native_host_start_doom(const am_native_doom_t *);
am_result_t app_manager_launch_doom_source(am_store_t *,const am_lease_t *,const am_metadata_t *,const char *,const am_native_doom_t *);
am_result_t am_native_doom_target_launch(am_store_t *,const am_lease_t *,const am_metadata_t *,const char *);
