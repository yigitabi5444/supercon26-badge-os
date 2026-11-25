#include "video_playback.h"

enum video_result video_play_pass(const vid2_asset_t *asset, const video_ops_t *ops,
                                  uint8_t *compressed, uint32_t compressed_capacity,
                                  uint8_t *pixels, uint32_t pixel_capacity)
{
    vid2_cursor_t cursor;
    if (!ops || !ops->now_us || !ops->wait_us || !ops->wipe || !ops->idle ||
        !ops->info || !ops->input || !ops->inflate || !ops->blit || !compressed ||
        !pixels || ops->top > ops->height || (ops->width & 7u) ||
        !vid2_open(asset, ops->width, ops->height - ops->top, &cursor)) return VIDEO_BAD_CLIP;

    void *ctx = ops->context;
    uint16_t x = (uint16_t)(((ops->width - cursor.width) / 16u) * 8u);
    uint16_t y = (uint16_t)(ops->top + (ops->height - ops->top - cursor.height) / 2u);
    ops->wipe(ctx);
    uint64_t start = ops->now_us(ctx);
    for (;;) {
        vid2_frame_t f;
        enum vid2_result r = vid2_next(asset, &cursor, &f);
        if (r == VID2_END) return VIDEO_REPEAT;
        if (r == VID2_INVALID || f.compressed_size > compressed_capacity ||
            f.decoded_size > pixel_capacity) break;

        uint64_t elapsed = ops->now_us(ctx) - start;
        if (elapsed <= f.late_after_us) {
            if (!asset->read(asset->context, f.offset, compressed, f.compressed_size)) break;
            if (ops->inflate(ctx, pixels, f.decoded_size, compressed, f.compressed_size) ==
                (int32_t)f.decoded_size) {
                elapsed = ops->now_us(ctx) - start;
                if (elapsed < f.due_us) ops->wait_us(ctx, f.due_us - elapsed);
                if (ops->now_us(ctx) - start <= f.late_after_us)
                    ops->blit(ctx, x, y, cursor.width, cursor.height, pixels);
            }
        }

        switch (ops->input(ctx)) {
        case VIDEO_INFO: {
            uint64_t paused = ops->now_us(ctx);
            ops->info(ctx);
            start += ops->now_us(ctx) - paused;
            break;
        }
        case VIDEO_RESTART:
            return VIDEO_REPEAT;
        case VIDEO_HOME:
            ops->idle(ctx);
            return VIDEO_EXIT_HOME;
        case VIDEO_NONE:
            break;
        }
    }
    ops->idle(ctx);
    return VIDEO_BAD_CLIP;
}
