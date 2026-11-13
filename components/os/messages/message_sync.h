// Two-badge anti-entropy, independent of ESP-NOW callback ownership.
#pragma once
#include "message_store.h"
#define MSG_FRAGMENT 200

typedef struct {
    bool receiving, transmitting;
    uint8_t peer[6], wanted[16], fragments, attempts, tx_peer[6], tx_fragment;
    uint8_t rx[MSG_RECORD_BYTES], tx[MSG_RECORD_BYTES], failed[16];
    uint32_t checksum;
    uint64_t retry_at, cooldown, offer_at, frame_at;
    unsigned offer;
} message_sync_t;
bool message_frame(const uint8_t *data, size_t size);
// Call only from the worker, under the store lock. Receive never owns/registers ESP-NOW callbacks.
// Returns true only for a fully reassembled, validated new record; caller must durably store it.
bool message_sync_receive(message_sync_t *s, const message_state_t *state, const uint8_t peer[6],
                          const uint8_t *packet, size_t size, uint64_t now_ms, message_t *complete);
void message_sync_tick(message_sync_t *s, const message_state_t *state, uint64_t now_ms, message_send_fn send);
