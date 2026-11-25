#ifndef BADGE_VIDEO_PLAYBACK_H
#define BADGE_VIDEO_PLAYBACK_H

#include "vid2.h"

enum video_key { VIDEO_NONE, VIDEO_INFO, VIDEO_RESTART, VIDEO_HOME };
enum video_result { VIDEO_REPEAT, VIDEO_EXIT_HOME, VIDEO_BAD_CLIP };

/* Internal adapter contract, not a public badge ABI. All functions are synchronous. */
typedef struct {
    void *context;
    uint16_t width, height, top;
    uint64_t (*now_us)(void *context);
    void (*wait_us)(void *context, uint64_t time_us);
    void (*wipe)(void *context);
    void (*idle)(void *context);
    void (*info)(void *context);
    enum video_key (*input)(void *context);
    int32_t (*inflate)(void *context, uint8_t *out, uint32_t out_size,
                       const uint8_t *in, uint32_t in_size);
    /* Host must take a frame lease and copy packed bits before return. Busy may drop the frame. */
    void (*blit)(void *context, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 const uint8_t *bits);
} video_ops_t;

/* Play one pass. The app repeats on end-of-clip or Restart, and handles Home on exit. */
enum video_result video_play_pass(const vid2_asset_t *asset, const video_ops_t *ops,
                                  uint8_t *compressed, uint32_t compressed_capacity,
                                  uint8_t *pixels, uint32_t pixel_capacity);

#endif
