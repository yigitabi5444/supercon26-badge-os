#include "doom_glue.h"

#include <limits.h>
#include <setjmp.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "d_event.h"
#include "g_game.h"
#include "i_video.h"
#include "w_file.h"

#define DOOM_APP_VERSION "1.0.1"
#ifdef __XTENSA__
BADGE_NATIVE_INFO("doom", DOOM_APP_VERSION);
#endif

const badge_doom_api_t *doom_api;
static badge_doom_lease_t lease;
static jmp_buf unwind;
static int result;
static badge_native_input_t buttons;
static uint32_t start_ms;

void *doom_realloc(void *old, size_t bytes)
{
    return bytes > UINT32_MAX ? NULL : doom_api->resize(doom_api->base.context, old, (uint32_t)bytes);
}
void *doom_malloc(size_t bytes) { return doom_realloc(NULL, bytes ? bytes : 1); }
void doom_free(void *old) { if (old) doom_realloc(old, 0); }
void *doom_calloc(size_t n, size_t width)
{
    if (width && n > SIZE_MAX / width) return NULL;
    size_t bytes = n * width;
    void *data = doom_malloc(bytes);
    if (data) memset(data, 0, bytes);
    return data;
}
char *doom_strdup(const char *s)
{
    size_t bytes = strlen(s) + 1;
    void *data = doom_malloc(bytes);
    return data ? memcpy(data, s, bytes) : NULL;
}

_Noreturn void doom_exit(int status) { result = status; longjmp(unwind, 1); }
int doom_system(const char *command) { (void)command; return -1; }

static wad_file_t *open_wad(char *path)
{
    extern wad_file_class_t stdc_wad_file;
    static wad_file_t file;
    if (strcmp(path, "doom1.wad")) return NULL;
    file = (wad_file_t){&stdc_wad_file, (byte *)lease.wad, lease.wad_bytes};
    return &file;
}
static void close_wad(wad_file_t *file) { (void)file; }
static size_t read_wad(wad_file_t *file, unsigned int offset, void *out, size_t bytes)
{
    if (offset >= file->length) return 0;
    if (bytes > file->length - offset) bytes = file->length - offset;
    memcpy(out, file->mapped + offset, bytes);
    return bytes;
}
wad_file_class_t stdc_wad_file = {open_wad, close_wad, read_wad};

void DG_Init(void) {}
void DG_SetWindowTitle(const char *label) { (void)label; }
uint32_t DG_GetTicksMs(void) { return doom_api->base.now_ms(doom_api->base.context) - start_ms + 1u; }
void DG_SleepMs(uint32_t delay) { doom_api->base.wait_ms(doom_api->base.context, delay); }
void DG_DrawFrame(void)
{
    static uint8_t rgb[BADGE_DOOM_RGB_BYTES];
    for (unsigned i = 0; i < 256; i++) {
        rgb[i * 3] = colors[i].r;
        rgb[i * 3 + 1] = colors[i].g;
        rgb[i * 3 + 2] = colors[i].b;
    }
    doom_api->present(doom_api->base.context, DG_ScreenBuffer, rgb);
}

/* Reuse the original Doom app's control adapter with the ABI2 input snapshot. */
enum { BTN_PLUS, BTN_MINUS, BTN_A, BTN_B };
#define CHORD_MS 120
#define KNOB_DIR 1
static int btn_held(void)
{
    doom_api->base.input(doom_api->base.context, &buttons);
    if (buttons.exit_requested) doom_exit(0);
    return (int)buttons.held;
}
static int knob_angle(void) { return buttons.knob; }
static int64_t esp_timer_get_time(void) { return (int64_t)DG_GetTicksMs() * 1000; }
#include "original_controls.inc"

__attribute__((visibility("default")))
int app_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const badge_native_api_t *base = badge_app_get_api(BADGE_NATIVE_ABI, sizeof(badge_doom_api_t));
    if (!base || base->abi_version != BADGE_NATIVE_ABI || base->struct_size < sizeof(badge_doom_api_t)) return -1;
    const badge_doom_api_t *api = (const badge_doom_api_t *)base;
    if (!base->context || !base->input || !base->now_ms || !base->wait_ms || !base->wipe || !base->frame_idle ||
        !api->get_lease || !api->resize || !api->present || !api->save_read || !api->save_replace) return -1;
    if (api->get_lease(base->context, &lease) != 1 || !lease.wad || lease.wad_bytes != BADGE_DOOM_WAD_BYTES ||
        memcmp(lease.wad, "IWAD", 4) || !lease.zone_mib || lease.zone_mib > INT_MAX / (1024u * 1024u)) return -1;
    doom_api = api;
    base->wipe(base->context);
    start_ms = base->now_ms(base->context);
    if (!setjmp(unwind)) {
        char mb[12];
        doom_snprintf(mb, sizeof mb, "%u", lease.zone_mib);
        char *args[] = {"doom", "-iwad", "doom1.wad", "-mb", mb, "-nomusic", "-warp", "1", "1", "-skill", "3", "-nogui", NULL};
        doomgeneric_Create(12, args);
        for (;;) {
            base->input(base->context, &buttons);
            if (buttons.exit_requested) doom_exit(0);
            doomgeneric_Tick();
            DG_SleepMs(1);
        }
    }
    doom_save_reset();
    base->frame_idle(base->context);
    lease = (badge_doom_lease_t){0};
    doom_api = NULL;
    return result;
}
