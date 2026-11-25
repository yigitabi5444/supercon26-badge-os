#pragma once

#include <stddef.h>
#include "badge_native_api.h"

enum {
    BADGE_DOOM_WAD_BYTES = 4196020,
    BADGE_DOOM_FRAME_BYTES = 320 * 200,
    BADGE_DOOM_RGB_BYTES = 256 * 3,
    BADGE_DOOM_SAVE_SLOTS = 6,
    BADGE_DOOM_RECOVERY_SAVE = BADGE_DOOM_SAVE_SLOTS,
    BADGE_DOOM_READ_MAX = 4096,
    BADGE_DOOM_SAVE_MAX = 0x2c000,
};

typedef struct {
    const uint8_t *wad;
    uint32_t wad_bytes;
    uint32_t zone_mib;
} badge_doom_lease_t;

/* Doom uses the existing ABI2 getter with this size. Its base.input latches
   upper-button Home; lower A+B remains Use. The host owns session cleanup. */
typedef struct {
    badge_native_api_t base;

    /* Return 1 with the validated read-only IWAD view kept alive until return. */
    int32_t (*get_lease)(void *context, badge_doom_lease_t *out);

    /* Session-owned allocation; NULL allocates, zero frees, failure preserves old. */
    void *(*resize)(void *context, void *old, uint32_t bytes);

    /* Copy fixed indices and RGB triples synchronously. 1 accepted, 0 panel busy. */
    int32_t (*present)(void *context, const uint8_t indices[BADGE_DOOM_FRAME_BYTES],
                      const uint8_t rgb[BADGE_DOOM_RGB_BYTES]);

    /* Slot 0..5 or recovery 6. Return bytes copied, 0 EOF, or -1 missing/failure. */
    int32_t (*save_read)(void *context, uint32_t slot, uint32_t offset,
                        uint8_t *out, uint32_t bytes);

    /* Consume the supplied bytes before return. 1 atomically committed, 0 failed. */
    int32_t (*save_replace)(void *context, uint32_t slot, const uint8_t *data, uint32_t bytes);
} badge_doom_api_t;

_Static_assert(offsetof(badge_doom_api_t, get_lease) == sizeof(badge_native_api_t), "Native prefix unchanged");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(badge_doom_lease_t) == 12, "ESP32-S3 Doom lease layout");
_Static_assert(sizeof(badge_doom_api_t) == 92, "ESP32-S3 Doom API layout");
#endif
