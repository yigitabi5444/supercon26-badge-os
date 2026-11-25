#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Module-local integration seam for this projection, not a Badge OS ABI.
typedef enum { CB_RECV_USB, CB_RECV_LAN, CB_RECV_INVALID } cb_receive_mode_t;
typedef enum {
    CB_FETCH_OK, CB_FETCH_INIT_FAILED, CB_FETCH_OFFLINE, CB_FETCH_HTTP,
    CB_FETCH_TOO_BIG, CB_FETCH_INCOMPLETE
} cb_fetch_result_t;
typedef enum {
    CB_NO_BUTTON, CB_PLUS, CB_MINUS, CB_A, CB_B, CB_EXIT_REQUESTED
} cb_button_t;

typedef struct {
    void *context;
    cb_receive_mode_t (*mode)(void *);
    int64_t (*now_us)(void *);
    cb_button_t (*button)(void *, unsigned timeout_ms);
    void (*red_leds)(void *, bool lit);
    bool (*seen_load)(void *, char *out, size_t capacity);
    bool (*seen_save)(void *, const char *value);
    bool (*lan_join)(void *, unsigned timeout_ms);
    // One complete bounded response from the configured Codex Board URL.
    // At most capacity bytes; no terminator required and no redirects.
    // Host must not retain the module-owned buffer after this call.
    cb_fetch_result_t (*lan_fetch)(void *, char *out, size_t capacity, size_t *written);
    bool (*usb_begin)(void *); // Host owns driver/READY; module owns frame parsing.
    size_t (*usb_read)(void *, char *out, size_t capacity);
    void (*usb_ack)(void *, const char *validated_revision);
    void (*release)(void *, cb_receive_mode_t); // release app I/O, host driver lifetime is separate
    void (*view_idle)(void *);
} cb_native_io_t;

// Run once from the foreground module task. Returns false if the binding is
// incomplete, with no I/O. Normal launch/exit validation stays with its host.
bool cb_native_run(const cb_native_io_t *io);
