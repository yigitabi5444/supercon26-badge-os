// Small atomic-blob persistence; uses only namespace badge_msg, key state.
#pragma once
#include "message_store.h"
#define MSG_SETTINGS_RESERVE_ENTRIES 192u
#define MSG_BLOB_ENTRY_BUDGET (3u + (MSG_SNAPSHOT_BYTES + 31u) / 32u)
typedef struct {
    void *ctx;
    int (*load)(void *, uint8_t *out, size_t *size); // capacity in, actual size out; 0 absent, 1 blob, -1 I/O, -2 size/type
    int (*save)(void *, const uint8_t *data, size_t size); // MSG_OK / FULL / IO, committed before OK
    size_t (*available)(void *); // NVS available_entries excludes its reserved GC page
    bool (*empty)(void *); // if no state blob, reject a namespace containing foreign keys
} message_blob_io_t;
typedef struct { message_blob_io_t io; size_t last_size; } message_blob_backend_t;
int message_blob_open(message_db_t *db, message_blob_backend_t *backend, message_blob_io_t io);
// Platform adapter. Does not initialize, repartition or erase NVS.
bool message_nvs_open(message_db_t *db, const char **status);
