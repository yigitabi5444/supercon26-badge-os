// Bounded public messages: phone-reported names/time are unverified. Store at most eight across all threads.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MSG_CAP 8
#define MSG_IGNORED 16
#define MSG_TEXT 240
#define MSG_TITLE 32
#define MSG_NAME 12

typedef struct {
    uint8_t id[16], thread[16], sender[6];
    uint64_t logical, seconds; // logical orders a thread; seconds only meaningful with clock == 1
    uint8_t clock;             // 0: unknown, 1: phone-reported UTC (unverified)
    char title[MSG_TITLE + 1], body[MSG_TEXT + 1], name[MSG_NAME + 1];
} message_t;
typedef struct {
    message_t messages[MSG_CAP]; uint8_t read[MSG_CAP], count, relay;
    uint64_t high_water; // retained across deletion, independent of wall time and storage generation
    uint8_t ignored[MSG_IGNORED][16], ignore_count; // recent local deletions, never relayed
} message_state_t;
enum { MSG_OK, MSG_DUP, MSG_INVALID, MSG_FULL, MSG_IO, MSG_FORMAT, MSG_CONFLICT, MSG_DELETED };
#define MSG_PACKET_MAX 227
// A service sender returns the transport result, never a delivery receipt.
typedef bool (*message_send_fn)(const uint8_t peer[6], const uint8_t *data, size_t size);

// Initialize once before the first ranging_start(); failure is nonfatal and preserves the store.
bool messaging_start(void);
// The existing ranging owner attaches after setup and detaches before ESP-NOW deinit/takeover.
void messaging_transport(message_send_fn send);
// Existing ESP-NOW callback forwards BMSG packets here: bounded nonblocking queue, no panel or flash access.
bool messaging_receive(const uint8_t peer[6], const uint8_t *data, size_t size);
// Cached internal-DRAM count (0..8), or -1 for unavailable storage. No lock, allocation, flash or redraw.
int messaging_unread(void);

// Each snapshot belongs to the caller. Mutations persist atomically or report an error.
bool messaging_snapshot(message_state_t *out, uint64_t *revision);
int messaging_submit(const uint8_t id[16], const uint8_t thread[16], const char *title,
                     const char *body, uint64_t seconds);
int messaging_read(const uint8_t thread[16]);
int messaging_forget(const uint8_t thread[16]);
int messaging_relay(bool enabled);
const char *messaging_status(void);
int message_find(const message_state_t *s, const uint8_t id[16]);
void message_hex(const uint8_t id[16], char out[33]);
bool message_unhex(const char *s, uint8_t id[16]);
