#include "badge_hackaday_api.h"
#include "had_native_controller.h"
#include <string.h>

#define HACKADAY_VERSION "1.0.0"
BADGE_NATIVE_INFO("hackaday", HACKADAY_VERSION);

static int key(void *opaque, unsigned timeout)
{
    const badge_hackaday_api_t *api = opaque;
    switch (api->key(api->base.context, timeout)) {
    case BADGE_HAD_EXIT: return HAD_EXIT_REQUESTED;
    case BADGE_HAD_PLUS: return HAD_PLUS;
    case BADGE_HAD_MINUS: return HAD_MINUS;
    case BADGE_HAD_A: return HAD_A;
    case BADGE_HAD_B: return HAD_B;
    default: return HAD_NO_KEY;
    }
}

static void keys(void *opaque, const char *p, const char *m, const char *a, const char *b)
{
    const badge_hackaday_api_t *api = opaque;
    api->keys(api->base.context, p, m, a, b);
}

static void *allocate(void *opaque, size_t size)
{
    const badge_hackaday_api_t *api = opaque;
    if (!size || size > BADGE_HAD_FEED_MAX) return NULL;
    return api->alloc(api->base.context, (uint32_t)size);
}

static void deallocate(void *opaque, void *ptr)
{
    const badge_hackaday_api_t *api = opaque;
    api->dealloc(api->base.context, ptr);
}

static bool begin_feed(void *opaque)
{
    const badge_hackaday_api_t *api = opaque;
    return api->begin_feed(api->base.context) == 1;
}

static bool begin_image(void *opaque, const char *url, unsigned width, unsigned height)
{
    const badge_hackaday_api_t *api = opaque;
    return api->begin_image(api->base.context, url, width, height) == 1;
}

static had_job_status_t poll(void *opaque, had_result_t *result)
{
    const badge_hackaday_api_t *api = opaque;
    badge_had_result_t wire = {0};
    int32_t status = api->poll(api->base.context, &wire);
    result->wifi_joined = wire.wifi_joined != 0;
    result->join_ms = wire.join_ms;
    result->bytes = wire.bytes;
    if (status == BADGE_HAD_PENDING) return HAD_PENDING;
    if (status == BADGE_HAD_OK) {
        result->data = wire.data;
        result->length = wire.length;
        result->width = wire.width;
        result->height = wire.height;
        return HAD_OK;
    }
    memcpy(result->error, wire.error, sizeof result->error);
    result->error[sizeof result->error - 1] = '\0';
    return HAD_FAILED;
}

static bool end_job(void *opaque, bool close_link)
{
    const badge_hackaday_api_t *api = opaque;
    return api->end_job(api->base.context, close_link) == 1;
}

static void blit_mono(void *opaque, int x, int y, unsigned width, unsigned height,
                      const uint8_t *bits, size_t length)
{
    const badge_hackaday_api_t *api = opaque;
    api->blit_mono(api->base.context, x, y, width, height, bits, (uint32_t)length);
}

__attribute__((visibility("default"),aligned(4)))
int app_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const badge_native_api_t *base = badge_app_get_api(BADGE_NATIVE_ABI, sizeof(badge_hackaday_api_t));
    if (!base || base->abi_version != BADGE_NATIVE_ABI ||
        base->struct_size < sizeof(badge_hackaday_api_t)) return -1;
    const badge_hackaday_api_t *api = (const badge_hackaday_api_t *)base;
    if (!api->key || !api->keys || !api->alloc || !api->dealloc || !api->begin_feed || !api->begin_image ||
        !api->poll || !api->end_job || !api->blit_mono || !api->release) return -1;
    const had_native_io_t binding = {
        .context = (void *)api, .view = base, .key = key, .keys = keys,
        .alloc = allocate, .dealloc = deallocate, .begin_feed = begin_feed,
        .begin_image = begin_image, .poll = poll, .end_job = end_job, .blit_mono = blit_mono,
    };
    bool read_ok = had_native_run(&binding);
    int32_t released = api->release(api->base.context);
    return read_ok && released == 1 ? 0 : -1;
}
