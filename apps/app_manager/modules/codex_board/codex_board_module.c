#include "badge_codex_api.h"
#include "board_alert.h"
#include "cb_native_controller.h"
#include "cb_native_view.h"

#define CODEX_BOARD_VERSION "1.0.0"
BADGE_NATIVE_INFO("codex_board", CODEX_BOARD_VERSION);

_Static_assert(CB_BODY_MAX == BADGE_CODEX_BODY_MAX, "Codex Board feed limit");
_Static_assert(CB_SEEN_TEXT == BADGE_CODEX_SEEN_MAX, "Codex Board seen format");

static cb_receive_mode_t mode(void *opaque)
{
    const badge_codex_api_t *api = opaque;
    switch (api->mode(api->base.context)) {
    case BADGE_CODEX_USB: return CB_RECV_USB;
    case BADGE_CODEX_LAN: return CB_RECV_LAN;
    default: return CB_RECV_INVALID;
    }
}

static int64_t now_us(void *opaque)
{
    const badge_codex_api_t *api = opaque;
    return (int64_t)api->now_us(api->base.context);
}

static cb_button_t button(void *opaque, unsigned timeout_ms)
{
    const badge_codex_api_t *api = opaque;
    switch (api->button(api->base.context, timeout_ms)) {
    case BADGE_CODEX_PLUS: return CB_PLUS;
    case BADGE_CODEX_MINUS: return CB_MINUS;
    case BADGE_CODEX_A: return CB_A;
    case BADGE_CODEX_B: return CB_B;
    case BADGE_CODEX_EXIT: return CB_EXIT_REQUESTED;
    default: return CB_NO_BUTTON;
    }
}

static void red_leds(void *opaque, bool lit)
{
    const badge_codex_api_t *api = opaque;
    api->red_leds(api->base.context, lit);
}

static bool seen_load(void *opaque, char *out, size_t capacity)
{
    const badge_codex_api_t *api = opaque;
    return api->seen_load(api->base.context, out, (uint32_t)capacity) == 1;
}

static bool seen_save(void *opaque, const char *value)
{
    const badge_codex_api_t *api = opaque;
    return api->seen_save(api->base.context, value) == 1;
}

static bool lan_join(void *opaque, unsigned timeout_ms)
{
    const badge_codex_api_t *api = opaque;
    return timeout_ms == 5000 && api->lan_join(api->base.context) == 1;
}

static cb_fetch_result_t lan_fetch(void *opaque, char *out, size_t capacity, size_t *written)
{
    const badge_codex_api_t *api = opaque;
    int32_t result = api->lan_fetch(api->base.context, out, (uint32_t)capacity);
    switch (result) {
    case BADGE_CODEX_FETCH_INIT: return CB_FETCH_INIT_FAILED;
    case BADGE_CODEX_FETCH_OFFLINE: return CB_FETCH_OFFLINE;
    case BADGE_CODEX_FETCH_HTTP: return CB_FETCH_HTTP;
    case BADGE_CODEX_FETCH_TOO_BIG: return CB_FETCH_TOO_BIG;
    case BADGE_CODEX_FETCH_INCOMPLETE: return CB_FETCH_INCOMPLETE;
    default:
        if (result < 0 || (size_t)result > capacity) return CB_FETCH_INCOMPLETE;
        *written = (size_t)result;
        return CB_FETCH_OK;
    }
}

static bool usb_begin(void *opaque)
{
    const badge_codex_api_t *api = opaque;
    return api->usb_begin(api->base.context) == 1;
}

static size_t usb_read(void *opaque, char *out, size_t capacity)
{
    const badge_codex_api_t *api = opaque;
    if (!capacity || capacity > BADGE_CODEX_USB_READ_MAX) return 0;
    int32_t result = api->usb_read(api->base.context, out, (uint32_t)capacity);
    return result > 0 ? (size_t)result : 0;
}

static void usb_ack(void *opaque, const char *revision)
{
    const badge_codex_api_t *api = opaque;
    api->usb_ack(api->base.context, revision);
}

static void release(void *opaque, cb_receive_mode_t transport)
{
    const badge_codex_api_t *api = opaque;
    (void)transport;
    api->release(api->base.context);
}

static void view_idle(void *opaque)
{
    const badge_codex_api_t *api = opaque;
    api->base.frame_idle(api->base.context);
}

__attribute__((visibility("default"),aligned(4)))
int app_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const badge_native_api_t *base = badge_app_get_api(BADGE_NATIVE_ABI, sizeof(badge_codex_api_t));
    if (!base || base->abi_version != BADGE_NATIVE_ABI || base->struct_size < sizeof(badge_codex_api_t) ||
        base->width != 960 || base->height != 672 || base->top != 48 ||
        !base->begin_frame || !base->clear || !base->box || !base->text ||
        !base->submit_frame || !base->frame_idle) return -1;
    const badge_codex_api_t *api = (const badge_codex_api_t *)base;
    if (!api->mode || !api->now_us || !api->button || !api->red_leds ||
        !api->seen_load || !api->seen_save || !api->lan_join || !api->lan_fetch ||
        !api->usb_begin || !api->usb_read || !api->usb_ack ||
        !api->release || !api->keys) return -1;

    const cb_native_io_t binding = {
        .context = (void *)api, .mode = mode, .now_us = now_us, .button = button, .red_leds = red_leds,
        .seen_load = seen_load, .seen_save = seen_save, .lan_join = lan_join, .lan_fetch = lan_fetch,
        .usb_begin = usb_begin, .usb_read = usb_read, .usb_ack = usb_ack, .release = release, .view_idle = view_idle,
    };
    cb_view_bind(base, api->keys);
    bool ok = cb_native_run(&binding);
    cb_view_bind(NULL, NULL);
    return ok ? 0 : -1;
}
