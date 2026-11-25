#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "txn_geometry.h"
#include "txn_vfs.h"

typedef struct {
    am_vfs_t vfs; // First member, common VFS context also identifies this adapter.
    StaticSemaphore_t mutex_storage;SemaphoreHandle_t mutex;
    bool mounted,committed,readonly;bool (*cancel)(void *);void *cancel_context;
} am_littlefs_t;
// Single owning app task initializes once; context lives through all calls/leases.
am_result_t am_littlefs_init(am_littlefs_t *);
// No compiled layout policy or key exists. Unqualified returns before even a flash read.
// Caller preconfigures crypto/runtime/workspace; invocation is not wired to native UI.
am_result_t am_littlefs_open(am_littlefs_t *,const am_layout_policy_t *,am_store_t *,
                             bool (*cancel)(void *),void *);
am_result_t am_littlefs_open_readonly(am_littlefs_t *,const am_layout_policy_t *,am_store_t *);
am_result_t am_littlefs_close(am_littlefs_t *);
// Same qualified volume/mutex, different fixed namespace; never during a live lease.
am_result_t am_littlefs_select(am_littlefs_t *,am_store_t *,const char *id,bool create);
