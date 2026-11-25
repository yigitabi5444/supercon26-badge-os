#include "badge_video_api.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STBI_ONLY_ZLIB
#define STBI_SUPPORT_ZLIB
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

int app_main(int argc, char **argv);

typedef struct {
    const uint8_t *asset;
    uint32_t asset_size;
    unsigned reads, fail_read, max_read, decodes, waits, polls, begins, busy_begin;
    unsigned submitted, wipes, idles, infos, exit_poll;
    bool lease, controls;
    uint64_t now, shown[60], hash;
} host_t;

static uint64_t now_us(void *ctx) { return ((host_t *)ctx)->now; }
static void wait_us(void *ctx, uint32_t us)
{
    host_t *h = ctx;
    assert(!h->lease && us && us <= 3000000);
    h->waits++;
    h->now += us;
}
static uint32_t asset_size(void *ctx, uint32_t index)
{
    assert(index == BADGE_VIDEO_CLIP);
    return ((host_t *)ctx)->asset_size;
}
static int32_t asset_read(void *ctx, uint32_t index, uint32_t off, uint8_t *out, uint32_t n)
{
    host_t *h = ctx;
    assert(index == BADGE_VIDEO_CLIP && !h->lease && n && n <= 4096);
    assert(off <= h->asset_size && n <= h->asset_size - off);
    if (++h->reads == h->fail_read) return 0;
    if (n > h->max_read) h->max_read = n;
    memcpy(out, h->asset + off, n);
    return 1;
}
static int32_t inflate(void *ctx, uint8_t *out, uint32_t out_n, const uint8_t *in, uint32_t in_n)
{
    host_t *h = ctx;
    assert(!h->lease && out_n == BADGE_VIDEO_PIXELS && in_n <= BADGE_VIDEO_COMPRESSED_MAX);
    h->decodes++;
    return stbi_zlib_decode_buffer((char *)out, (int)out_n, (const char *)in, (int)in_n);
}
static int32_t begin_frame(void *ctx)
{
    host_t *h = ctx;
    assert(!h->lease);
    h->lease = ++h->begins != h->busy_begin;
    return h->lease;
}
static int32_t blit(void *ctx, int32_t x, int32_t y, uint32_t w, uint32_t height,
                    const uint8_t *bits, uint32_t n)
{
    host_t *h = ctx;
    assert(h->lease && x == 64 && y == 48 && w == 832 && height == 624 && n == BADGE_VIDEO_PIXELS);
    h->shown[h->submitted] = h->now;
    for (uint32_t i = 0; i < n; i++) h->hash = (h->hash ^ bits[i]) * UINT64_C(0x100000001b3);
    return 1;
}
static void submit_frame(void *ctx)
{
    host_t *h = ctx;
    assert(h->lease && h->submitted < 60);
    h->lease = false;
    h->submitted++;
}
static void wipe(void *ctx) { host_t *h = ctx; assert(!h->lease); h->wipes++; }
static void idle(void *ctx) { host_t *h = ctx; assert(!h->lease); h->idles++; }
static int32_t info(void *ctx)
{
    host_t *h = ctx;
    assert(!h->lease);
    h->infos++;
    h->now += 2000000;
    return 1;
}
static void input(void *ctx, badge_native_input_t *buttons)
{
    host_t *h = ctx;
    assert(!h->lease && h->polls < 60);
    *buttons = (badge_native_input_t){.knob = -1};
    if (++h->polls == h->exit_poll) buttons->exit_requested = 1;
    if (h->controls && h->polls == 1) buttons->pressed = NATIVE_PLUS;
    if (h->controls && h->polls == 2) buttons->pressed = NATIVE_A;
}

static badge_video_api_t api = {
    .base = {.abi_version = BADGE_NATIVE_ABI, .struct_size = sizeof api,
             .width = 960, .height = 672, .top = 48, .input = input,
             .begin_frame = begin_frame, .submit_frame = submit_frame,
             .frame_idle = idle, .wipe = wipe},
    .now_us = now_us, .wait_us = wait_us, .asset_size = asset_size,
    .asset_read = asset_read, .inflate = inflate, .blit = blit, .info = info,
};
static bool getter_enabled = true;

const badge_native_api_t *badge_app_get_api(uint32_t abi, uint32_t size)
{
    assert(abi == BADGE_NATIVE_ABI && size == sizeof api);
    return getter_enabled ? &api.base : NULL;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb");
    uint8_t *clip = malloc(BADGE_VIDEO_CLIP_BYTES);
    assert(f && clip && fread(clip, 1, BADGE_VIDEO_CLIP_BYTES, f) == BADGE_VIDEO_CLIP_BYTES);
    assert(fgetc(f) == EOF && !ferror(f));
    fclose(f);
    host_t h = {.asset = clip, .asset_size = BADGE_VIDEO_CLIP_BYTES, .exit_poll = 60,
                .hash = UINT64_C(0xcbf29ce484222325)};
    const host_t initial = h;
    api.base.context = &h;
    assert(app_main(0, NULL) == 0);
    assert(h.wipes == 1 && h.submitted == 60 && h.decodes == 60 && h.idles == 1);
    assert(h.max_read == 4096 && h.reads > 240 && h.waits == 59);
    assert(h.hash == UINT64_C(0x1ad0e814ffcd8b36));
    for (unsigned i = 0; i < 60; i++) assert(h.shown[i] == (uint64_t)i * 250000);

    h = initial;
    h.controls = true;
    h.busy_begin = 3;
    h.exit_poll = 4;
    assert(app_main(0, NULL) == 0);
    assert(h.wipes == 2 && h.infos == 1 && h.submitted == 3 && h.decodes == 4 && h.idles == 1);
    assert(h.shown[0] == 0 && h.shown[1] == 2250000 && h.shown[2] == 2500000);

    h = initial;
    h.fail_read = 4;
    assert(app_main(0, NULL) == -1 && h.reads == 4 && h.wipes == 1 && h.idles == 1);
    assert(!h.decodes && !h.submitted);
    h = initial;
    h.asset_size = 0;
    assert(app_main(0, NULL) == -1 && !h.reads && !h.wipes);
    api.base.struct_size = sizeof api.base;
    assert(app_main(0, NULL) == -1);
    api.base.struct_size = sizeof api;
    getter_enabled = false;
    assert(app_main(0, NULL) == -1);
    free(clip);
    puts("Video module: exact 60 frames at 4 fps, bounded reads, Info, Restart, busy frame, Home and failure paths passed");
}
