#include "message_nvs.h"
#include <stdlib.h>
#include <string.h>

static int persist(void *ctx, const message_state_t *state, uint64_t generation)
{
    message_blob_backend_t *b = ctx;
    uint8_t *data = malloc(MSG_SNAPSHOT_BYTES), *verify = malloc(MSG_SNAPSHOT_BYTES);
    if (!data || !verify) { free(data); free(verify); return MSG_IO; }
    size_t size = message_pack(state, generation, data), actual = MSG_SNAPSHOT_BYTES;
    size_t needed = 3 + (size + 31) / 32;
    // Shrinking/equal-size changes can free space or mark read without consuming more Settings budget.
    if (size > b->last_size) needed += MSG_SETTINGS_RESERVE_ENTRIES;
    if (b->io.available(b->io.ctx) < needed) { free(data); free(verify); return MSG_FULL; }
    int result = b->io.save(b->io.ctx, data, size);
    if (result == MSG_OK && (b->io.load(b->io.ctx, verify, &actual) != 1 || actual != size ||
        memcmp(data, verify, size))) result = MSG_IO;
    if (result == MSG_OK) b->last_size = size;
    free(data); free(verify); return result;
}
int message_blob_open(message_db_t *db, message_blob_backend_t *b, message_blob_io_t io)
{
    memset(db, 0, sizeof *db); b->io = io;
    uint8_t *data = malloc(MSG_SNAPSHOT_BYTES);
    if (!data) return MSG_IO;
    size_t size = MSG_SNAPSHOT_BYTES;
    int found = io.load(io.ctx, data, &size), result = MSG_OK;
    b->last_size = found == 1 ? size : 0;
    if (found == 1) {
        if (!message_unpack(&db->state, &db->generation, data, size)) result = MSG_FORMAT;
    } else if (found == -2 || (found == 0 && !io.empty(io.ctx))) result = MSG_FORMAT;
    else if (found < 0) result = MSG_IO;
    else if (io.available(io.ctx) < MSG_SETTINGS_RESERVE_ENTRIES + 2 * MSG_BLOB_ENTRY_BUDGET) result = MSG_FULL;
    free(data);
    if (result != MSG_OK) return result;
    // Only an absent, empty namespace gets the new default. Persisted OFF stays OFF.
    if (found == 0) db->state.relay = true;
    db->ready = true; db->persist = persist; db->persist_ctx = b;
    return MSG_OK;
}
