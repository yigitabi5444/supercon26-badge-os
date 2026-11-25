#pragma once
#include "manager_ui.h"
#include "txn.h"

typedef struct {
    void *context;bool assigned;
    am_result_t (*open)(void *,am_store_t *,bool (*)(void *),void *);
    am_result_t (*close)(void *);
    am_result_t (*incoming)(void *,am_package_t *); // Immutable spans until operation returns.
    bool source_ready;
    am_result_t (*fetch)(void *,bool (*)(void *),void *); // Explicit request only, no filesystem I/O.
    void (*discard)(void *); // Quiescent cache release after store close; never during a lease.
    void *(*allocate)(void *,size_t,bool); // execution=true requests aligned private executable copy.
    void (*free)(void *,void *);
    void (*home_mode)(void *,bool);
    bool (*home_request)(void *); // Consuming API: controller retains a sticky copy.
    bool (*home_end)(void *); // Atomic capture + disable + clear. Required before mutations.
    am_result_t (*launch)(void *,const uint8_t *,size_t,const am_metadata_t *);
    am_result_t (*quiesce)(void *); // AM_BUSY retains live loader/copy/lease; retry on owning task.
    void (*progress)(void *,const char *); // Owning task, before transaction work begins.
    am_result_t (*select)(void *,am_store_t *,const char *,bool,bool *); // Fixed ID, no lease; create only for install.
    am_result_t (*prepare)(void *,am_store_t *,bool (*)(void *),void *); // Required assets before active code commit.
    am_result_t (*launch_doom)(void *,am_store_t *,am_lease_t *,const am_metadata_t *,const char *);
} am_platform_t;
enum am_controller_state { AM_UNASSIGNED,AM_READY,AM_FAULT,AM_CLOSED };
typedef struct {
    am_platform_t platform;am_store_t store;am_lease_t lease;
    am_local_t local[AM_COUNT];char version[AM_COUNT][21],package_version[AM_COUNT][21];
    unsigned selected;
    uint64_t used,capacity;enum am_controller_state state;
    uint8_t *execution_copy;
    am_result_t last;bool opened,busy,deferred,home_pending,exit_requested;
} am_controller_t; // Stable address, single owning task; never copy an active context.

am_result_t am_controller_init(am_controller_t *,am_platform_t,am_runtime_t);
am_result_t am_controller_refresh(am_controller_t *);
am_result_t am_controller_action(am_controller_t *,unsigned,enum am_action);
am_result_t am_controller_shutdown(am_controller_t *);
bool am_controller_cancel(void *);
const char *am_controller_notice(am_result_t);
am_result_t am_target_start(am_controller_t *);
am_result_t app_manager_launch_snapshot(const uint8_t *,size_t,const am_metadata_t *);
am_result_t app_manager_quiesce_snapshot(void);
