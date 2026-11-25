/* Portable projection of badge-ux-integration 9b73810 Codex Board. */
#include "cb_native_controller.h"
#include "board_alert.h"
#include "board_stream.h"
#include "board_view.h"
#include <string.h>

static const cb_native_io_t *io;

#define POLL_US 3000000LL
#define STALE_US 30000000LL
#define FLASH_US 5000000LL
#define FLASH_HALF_PERIOD_US 250000LL

// The home task has a 16 KB stack. Keep HTTP buffers and snapshots out of it.
static union { char body[CB_BODY_MAX + 1]; cb_stream_t stream; } transport;
static char saved[CB_SEEN_TEXT];
static cb_feed_t current, received;
static cb_seen_t seen;
static struct { int64_t started; bool active, lit; } flash;

static void flash_set(bool lit)
{
    if (flash.lit == lit) return;
    io->red_leds(io->context, lit);
    flash.lit = lit;
}

static void flash_stop(void)
{
    flash.active = false;
    flash_set(false);
}

static void red_flash(void)
{
    flash.started = io->now_us(io->context);
    flash.active = true;
    flash_set(true);
}

static void flash_tick(void)
{
    if (!flash.active) return;
    int64_t elapsed = io->now_us(io->context) - flash.started;
    if (elapsed >= FLASH_US) flash_stop();
    else flash_set(((uint32_t)elapsed / (uint32_t)FLASH_HALF_PERIOD_US) % 2 == 0);
}

static const char *fetch(void)
{
    size_t count = 0;
    switch (io->lan_fetch(io->context, transport.body, CB_BODY_MAX, &count)) {
    case CB_FETCH_INIT_FAILED: return "HTTP initialization failed";
    case CB_FETCH_HTTP: return "HTTP error: retaining last snapshot";
    case CB_FETCH_TOO_BIG: return "Feed exceeds 8192 bytes";
    case CB_FETCH_INCOMPLETE: return "Incomplete feed: retaining last snapshot";
    case CB_FETCH_OFFLINE: return "OFFLINE: retaining last snapshot";
    case CB_FETCH_OK:
        if (count > CB_BODY_MAX) return "Incomplete feed: retaining last snapshot";
        return cb_feed_parse(transport.body, count, &received) ? "" : "Invalid feed: retaining last snapshot";
    default: return "OFFLINE: retaining last snapshot";
    }
}

static char input[256];
static unsigned input_size, input_pos;

static bool cb_usb_start(void)
{
    if (!io->usb_begin(io->context)) return false;
    cb_stream_reset(&transport.stream);
    input_size = input_pos = 0;
    return true;
}

static cb_stream_result_t cb_usb_poll(cb_feed_t *out)
{
    for (unsigned processed = 0; processed < 1024; processed++) {
        if (input_pos == input_size) {
            size_t n = io->usb_read(io->context, input, sizeof input);
            if (!n) return CB_STREAM_NONE;
            if (n > sizeof input) return CB_STREAM_INVALID;
            input_size = (unsigned)n;
            input_pos = 0;
        }
        cb_stream_result_t result = cb_stream_byte(&transport.stream, input[input_pos++], out);
        if (result != CB_STREAM_NONE) return result;
    }
    return CB_STREAM_NONE;
}

static void cb_usb_ack(const char *revision)
{
    io->usb_ack(io->context, revision);
}

typedef struct {
    bool have_feed, memory_ok, stale;
    unsigned page;
    char last_sample[41];
    int64_t sample_changed, last_received;
} board_state_t;

static bool accept_received(board_state_t *state, int64_t now)
{
    if (!state->have_feed || strcmp(state->last_sample, received.sampled)) {
        memcpy(state->last_sample, received.sampled, sizeof state->last_sample);
        state->sample_changed = now;
    }
    state->last_received = now;
    state->stale = now - state->sample_changed > STALE_US;
    bool draw = !state->have_feed || strcmp(current.revision, received.revision) ||
                current.source != received.source || current.quota != received.quota;
    if (state->have_feed) cb_feed_retain_terminals(&received, &current);
    current = received;
    state->have_feed = true;
    state->page %= cb_pages(&current);
    if (!state->stale && cb_seen_observe(&seen, &current)) {
        cb_seen_save(&seen, saved);
        // Save before flashing, so leaving/rebooting cannot replay this event.
        state->memory_ok = io->seen_save(io->context, saved);
        red_flash();
    }
    return draw;
}

static const char *healthy_notice(const board_state_t *state)
{
    return state->stale ? "STALE FEED: collector has stopped updating" :
           state->memory_ok ? "" : "Alert history could not be saved";
}

static void paint(const board_state_t *state, const char *notice, bool usb)
{
    if (state->have_feed) cb_view(&current, state->page, notice, usb);
    else cb_setup_view(notice[0] ? notice : "Connecting to the feed...", usb);
}

bool cb_native_run(const cb_native_io_t *binding)
{
    if (io || !binding || !binding->mode || !binding->now_us || !binding->button ||
        !binding->red_leds || !binding->seen_load || !binding->seen_save ||
        !binding->lan_join || !binding->lan_fetch || !binding->usb_begin ||
        !binding->usb_read || !binding->usb_ack || !binding->release ||
        !binding->view_idle) return false;
    io = binding;
    cb_receive_mode_t mode = io->mode(io->context);
    flash_stop();
    char previous_notice[80] = {0};
    board_state_t state = {.memory_ok = true};
    bool joined = false, usb_ready = false;
    int64_t next_poll = 0;
    const char *notice = "Connecting to the feed...";
    memset(&seen, 0, sizeof seen);
    if (io->seen_load(io->context, saved, sizeof saved)) {
        saved[sizeof saved - 1] = 0;
        cb_seen_load(&seen, saved);
    }
    bool usb = mode == CB_RECV_USB;
    if (usb) {
        usb_ready = cb_usb_start();
        notice = usb_ready ? "USB waiting: send a Codex Board feed" : "USB receiver could not start; press A";
    } else if (mode != CB_RECV_LAN) notice = "Set a valid http:// feed URL in Settings";
    paint(&state, notice, usb);
    for (;;) {
        cb_button_t key = io->button(io->context, usb ? 20u : 100u);
        flash_tick();
        bool draw = false;
        if (key == CB_B || key == CB_EXIT_REQUESTED) break;
        if (key == CB_A) {
            if (state.have_feed && current.message[0]) red_flash();
            else if (usb) {
                if (!state.have_feed) {
                    usb_ready = cb_usb_start();
                    notice = usb_ready ? "USB waiting: send a Codex Board feed" : "USB receiver could not start; press A";
                }
                draw = true;
            } else next_poll = 0;
        }
        if (state.have_feed && !current.message[0] && (key == CB_PLUS || key == CB_MINUS)) {
            unsigned total = cb_pages(&current);
            state.page = (state.page + total + (key == CB_PLUS ? 1 : -1)) % total;
            draw = true;
        }
        int64_t now = io->now_us(io->context);
        if (usb && usb_ready) {
            // A bounded amount of work per button loop, independent of HTTP polling.
            for (unsigned frames = 0; frames < 4; frames++) {
                cb_stream_result_t result = cb_usb_poll(&received);
                if (result == CB_STREAM_NONE) break;
                if (result == CB_STREAM_VALID) {
                    now = io->now_us(io->context);
                    draw |= accept_received(&state, now);
                    cb_usb_ack(current.revision);
                    notice = healthy_notice(&state);
                } else notice = "Invalid USB feed: retaining last snapshot";
            }
            if (state.have_feed && now - state.last_received > STALE_US)
                notice = "USB STALE: no complete feed for 30 seconds";
            else if (state.have_feed && now - state.sample_changed > STALE_US)
                notice = "STALE FEED: collector has stopped updating";
        } else if (mode == CB_RECV_LAN && !flash.active && now >= next_poll) {
            // HTTP can block for five seconds. Delay the next poll until the
            // LED sequence finishes; buttons and USB remain responsive.
            if (!joined) joined = io->lan_join(io->context, 5000);
            const char *error = joined ? fetch() : "Wi-Fi unavailable: check Settings";
            now = io->now_us(io->context);
            if (error[0]) {
                notice = error;
                // Wi-Fi also retries by itself; a later join verifies we have an IP.
                joined = false;
            } else {
                draw |= accept_received(&state, now);
                notice = healthy_notice(&state);
            }
            next_poll = now + POLL_US;
        }
        if (strcmp(previous_notice, notice)) {
            // All notices are fixed application strings shorter than this buffer.
            strcpy(previous_notice, notice);
            draw = true;
        }
        if (draw) paint(&state, notice, usb);
        flash_tick();
    }
    flash_stop();
    io->view_idle(io->context);
    io->release(io->context, mode);
    io = NULL;
    return true;
}
