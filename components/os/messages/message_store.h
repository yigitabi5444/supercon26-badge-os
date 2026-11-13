// Bounded v1 records and crash-safe snapshots. No ESP-IDF dependency.
#pragma once
#include "badge/messaging.h"
#define MSG_RECORD_BYTES 342
#define MSG_SNAPSHOT_BYTES (40 + MSG_CAP * (MSG_RECORD_BYTES + 1) + 1 + MSG_IGNORED * 16)

typedef int (*message_persist_fn)(void *, const message_state_t *, uint64_t generation);
typedef struct {
    message_state_t state; uint64_t generation; bool ready;
    void *persist_ctx; message_persist_fn persist; // atomic-blob backend; failed writes retain RAM state
} message_db_t;

uint32_t message_crc(const uint8_t *data, size_t size);
bool message_valid(const message_t *m);
void message_encode(const message_t *m, uint8_t out[MSG_RECORD_BYTES]);
bool message_decode(message_t *m, const uint8_t *in, size_t n);
size_t message_pack(const message_state_t *state, uint64_t generation, uint8_t out[MSG_SNAPSHOT_BYTES]);
bool message_unpack(message_state_t *state, uint64_t *generation, const uint8_t *data, size_t size);
bool message_ignored(const message_state_t *state, const uint8_t id[16]);
uint64_t message_next_logical(const message_state_t *s);
int message_put(message_db_t *db, const message_t *m, bool read);
int message_mark_read(message_db_t *db, const uint8_t thread[16]);
int message_relay(message_db_t *db, bool enabled);
int message_forget(message_db_t *db, const uint8_t thread[16]); // local only; copies can return
