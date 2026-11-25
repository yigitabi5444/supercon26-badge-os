#ifndef BADGE_VIDEO_VID2_H
#define BADGE_VIDEO_VID2_H

#include <stdbool.h>
#include <stdint.h>

/* A pinned, verified current-package asset. The reader cannot select a path. */
typedef struct {
    void *context;
    uint32_t size;
    bool (*read)(void *context, uint32_t offset, void *out, uint32_t size);
} vid2_asset_t;

typedef struct {
    uint32_t offset;
    uint16_t width, height, count, fps, next;
} vid2_cursor_t;

typedef struct {
    uint32_t offset, compressed_size, decoded_size;
    uint64_t due_us, late_after_us;
} vid2_frame_t;

enum vid2_result { VID2_FRAME, VID2_END, VID2_INVALID };

bool vid2_open(const vid2_asset_t *asset, uint32_t width, uint32_t height,
               vid2_cursor_t *cursor);
enum vid2_result vid2_next(const vid2_asset_t *asset, vid2_cursor_t *cursor,
                          vid2_frame_t *frame);

#endif
