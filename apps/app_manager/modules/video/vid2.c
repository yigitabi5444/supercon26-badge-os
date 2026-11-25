#include "vid2.h"

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t u32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool vid2_open(const vid2_asset_t *asset, uint32_t width, uint32_t height,
               vid2_cursor_t *cursor)
{
    uint8_t header[12];
    if (!asset || !asset->read || !cursor || asset->size < sizeof header ||
        !asset->read(asset->context, 0, header, sizeof header)) return false;
    if (header[0] != 'V' || header[1] != 'I' || header[2] != 'D' || header[3] != '2') return false;

    uint16_t w = u16(header + 4), h = u16(header + 6);
    uint16_t count = u16(header + 8), fps = u16(header + 10);
    if (!w || !h || (w & 7u) || w > width || h > height || !count || !fps ||
        count > (asset->size - sizeof header) / 5u) return false;
    *cursor = (vid2_cursor_t){.offset = sizeof header, .width = w, .height = h,
                              .count = count, .fps = fps};
    return true;
}

enum vid2_result vid2_next(const vid2_asset_t *asset, vid2_cursor_t *cursor,
                          vid2_frame_t *frame)
{
    if (!asset || !asset->read || !cursor || !frame || !cursor->fps ||
        cursor->next > cursor->count || cursor->offset > asset->size) return VID2_INVALID;
    if (cursor->next == cursor->count)
        return cursor->offset == asset->size ? VID2_END : VID2_INVALID;

    uint8_t length[4];
    if (asset->size - cursor->offset < sizeof length ||
        !asset->read(asset->context, cursor->offset, length, sizeof length)) return VID2_INVALID;
    uint32_t start = cursor->offset + sizeof length;
    uint32_t size = u32(length);
    if (!size || size > asset->size - start) return VID2_INVALID;
    uint32_t end = start + size;
    if (cursor->next + 1u == cursor->count && end != asset->size) return VID2_INVALID;

    uint32_t step = 1000000u / cursor->fps, fraction = 1000000u % cursor->fps;
    /* Both factors in the residual product are at most UINT16_MAX. */
    uint64_t due = (uint64_t)cursor->next * step + cursor->next * fraction / cursor->fps;
    *frame = (vid2_frame_t){.offset = start, .compressed_size = size,
                            .decoded_size = (uint32_t)(cursor->width / 8u) * cursor->height,
                            .due_us = due, .late_after_us = due + step};
    cursor->offset = end;
    cursor->next++;
    return VID2_FRAME;
}
