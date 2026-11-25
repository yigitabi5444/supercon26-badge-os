#pragma once
#include "badge_native_api.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Private module seam, not a Badge OS or package ABI.
typedef enum { HAD_PENDING, HAD_OK, HAD_FAILED } had_job_status_t;
enum { HAD_EXIT_REQUESTED = -2, HAD_NO_KEY = -1, HAD_PLUS, HAD_MINUS, HAD_A, HAD_B };
typedef struct {
    bool wifi_joined;
    unsigned join_ms;
    size_t bytes;
    const uint8_t *data; // Borrowed until end_job. Never passed to a worker by the module.
    size_t length;
    unsigned width, height; // For images: bits, MSB first, rows of (width + 7) / 8 bytes.
    char error[64];
} had_result_t;

typedef struct {
    void *context;
    const badge_native_api_t *view;
    int (*key)(void *, unsigned timeout_ms); // HAD_EXIT_REQUESTED is distinct from B.
    void (*keys)(void *, const char *, const char *, const char *, const char *);
    void *(*alloc)(void *, size_t);
    void (*dealloc)(void *, void *);
    bool (*begin_feed)(void *); // Fixed existing HTTPS RSS endpoint; decode bounded gzip if present.
    bool (*begin_image)(void *, const char *url, unsigned box_w, unsigned box_h); // Host copies URL.
    had_job_status_t (*poll)(void *, had_result_t *);
    // Consume or cancel the one job. Join/release it before returning.
    // close_link also returns the radio to ranging. False ends foreground job use.
    bool (*end_job)(void *, bool close_link);
    void (*blit_mono)(void *, int x, int y, unsigned w, unsigned h, const uint8_t *bits, size_t len);
} had_native_io_t;

bool had_native_run(const had_native_io_t *io);
