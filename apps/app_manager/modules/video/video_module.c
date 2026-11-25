// Adapt the existing Video app's packed VID2 playback to the installed app host.
#include "badge_video_api.h"
#include "video_playback.h"

#define VIDEO_APP_VERSION "1.0.0"
#if defined(__XTENSA__)
BADGE_NATIVE_INFO("video", VIDEO_APP_VERSION);
#endif

enum { VIDEO_READ_MAX = 4096, VIDEO_WAIT_MAX_US = 3000000 };
static uint8_t compressed[BADGE_VIDEO_COMPRESSED_MAX] __attribute__((aligned(4)));
static uint8_t pixels[BADGE_VIDEO_PIXELS] __attribute__((aligned(4)));

static bool read_clip(void *opaque, uint32_t offset, void *out, uint32_t size)
{
    const badge_video_api_t *api = opaque;
    uint8_t *bytes = out;
    while (size) {
        uint32_t chunk = size > VIDEO_READ_MAX ? VIDEO_READ_MAX : size;
        if (api->asset_read(api->base.context, BADGE_VIDEO_CLIP, offset, bytes, chunk) != 1)
            return false;
        offset += chunk;
        bytes += chunk;
        size -= chunk;
    }
    return true;
}

static uint64_t now_us(void *opaque)
{
    const badge_video_api_t *api = opaque;
    return api->now_us(api->base.context);
}

static void wait_us(void *opaque, uint64_t delay)
{
    const badge_video_api_t *api = opaque;
    api->wait_us(api->base.context, delay > VIDEO_WAIT_MAX_US ? VIDEO_WAIT_MAX_US : (uint32_t)delay);
}

static void wipe(void *opaque)
{
    const badge_video_api_t *api = opaque;
    api->base.wipe(api->base.context);
}

static void idle(void *opaque)
{
    const badge_video_api_t *api = opaque;
    api->base.frame_idle(api->base.context);
}

static void info(void *opaque)
{
    const badge_video_api_t *api = opaque;
    api->info(api->base.context);
}

static enum video_key input(void *opaque)
{
    const badge_video_api_t *api = opaque;
    badge_native_input_t buttons;
    api->base.input(api->base.context, &buttons);
    if (buttons.exit_requested) return VIDEO_HOME;
    if (buttons.pressed & NATIVE_PLUS) return VIDEO_INFO;
    if (buttons.pressed & NATIVE_A) return VIDEO_RESTART;
    return VIDEO_NONE;
}

static int32_t inflate(void *opaque, uint8_t *out, uint32_t out_size,
                       const uint8_t *in, uint32_t in_size)
{
    const badge_video_api_t *api = opaque;
    return api->inflate(api->base.context, out, out_size, in, in_size);
}

static void blit(void *opaque, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 const uint8_t *bits)
{
    const badge_video_api_t *api = opaque;
    void *ctx = api->base.context;
    if (!api->base.begin_frame(ctx)) return;
    api->blit(ctx, x, y, w, h, bits, BADGE_VIDEO_PIXELS);
    api->base.submit_frame(ctx);
}

__attribute__((visibility("default")))
int app_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const badge_native_api_t *base = badge_app_get_api(BADGE_NATIVE_ABI, sizeof(badge_video_api_t));
    if (!base || base->abi_version != BADGE_NATIVE_ABI || base->struct_size < sizeof(badge_video_api_t))
        return -1;
    const badge_video_api_t *api = (const badge_video_api_t *)base;
    if (!base->context || base->width != 960 || base->height != 672 || base->top != 48 ||
        !base->input || !base->begin_frame || !base->submit_frame || !base->frame_idle || !base->wipe ||
        !api->now_us || !api->wait_us || !api->asset_size || !api->asset_read ||
        !api->inflate || !api->blit || !api->info) return -1;
    uint32_t clip_size = api->asset_size(base->context, BADGE_VIDEO_CLIP);
    if (clip_size != BADGE_VIDEO_CLIP_BYTES) return -1;

    // The adapter context is only used to call the borrowed host table.
    vid2_asset_t asset = {(void *)api, clip_size, read_clip};
    video_ops_t ops = {(void *)api, base->width, base->height, base->top, now_us, wait_us,
                       wipe, idle, info, input, inflate, blit};
    enum video_result result;
    do {
        result = video_play_pass(&asset, &ops, compressed, sizeof compressed, pixels, sizeof pixels);
    } while (result == VIDEO_REPEAT);
    return result == VIDEO_EXIT_HOME ? 0 : -1;
}
