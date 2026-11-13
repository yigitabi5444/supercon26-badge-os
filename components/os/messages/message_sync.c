#include "message_sync.h"
#include <string.h>

enum { OFFER = 1, WANT, DATA };
bool message_frame(const uint8_t *p, size_t n) { return n >= 4 && !memcmp(p, "BMSG", 4); }
static void header(uint8_t *p, int type, const uint8_t id[16])
{ memcpy(p, "BMSG", 4); p[4] = 1; p[5] = type; memcpy(p + 6, id, 16); }
bool message_sync_receive(message_sync_t *s, const message_state_t *state, const uint8_t peer[6],
                          const uint8_t *p, size_t n, uint64_t now, message_t *out)
{
    if (!state->relay || n < 22 || memcmp(p, "BMSG", 4) || p[4] != 1) return false;
    uint8_t nonzero = 0; for (int i = 0; i < 16; i++) nonzero |= p[6+i];
    if (!nonzero || message_ignored(state, p + 6)) return false;
    if (p[5] == OFFER && n == 22 && !s->receiving && state->count < MSG_CAP &&
        message_find(state, p + 6) < 0 && (now >= s->cooldown || memcmp(p + 6, s->failed, 16))) {
        s->receiving = true; memcpy(s->wanted, p + 6, 16); memcpy(s->peer, peer, 6);
        s->fragments = s->attempts = 0; s->retry_at = now;
    } else if (p[5] == WANT && n == 22 && !s->transmitting) {
        int at = message_find(state, p + 6);
        if (at >= 0) {
            message_encode(&state->messages[at], s->tx); memcpy(s->tx_peer, peer, 6);
            s->transmitting = true; s->tx_fragment = 0;
        }
    } else if (p[5] == DATA && s->receiving && n >= 27 && !memcmp(peer, s->peer, 6) &&
               !memcmp(p + 6, s->wanted, 16) && p[22] < 2) {
        int fragment = p[22], size = fragment ? MSG_RECORD_BYTES - MSG_FRAGMENT : MSG_FRAGMENT;
        if (n != (size_t)(27 + size)) return false;
        uint32_t checksum = (uint32_t)p[23] | ((uint32_t)p[24] << 8) | ((uint32_t)p[25] << 16) | ((uint32_t)p[26] << 24);
        if (s->fragments && checksum != s->checksum) return false;
        s->checksum = checksum;
        memcpy(s->rx + fragment * MSG_FRAGMENT, p + 27, size); s->fragments |= 1 << fragment;
        if (s->fragments == 3) {
            s->receiving = false;
            return message_crc(s->rx, sizeof s->rx) == s->checksum && message_decode(out, s->rx, sizeof s->rx) && !memcmp(out->id, s->wanted, 16);
        }
    }
    return false;
}
void message_sync_tick(message_sync_t *s, const message_state_t *state, uint64_t now, message_send_fn send)
{
    if (!state->relay) { memset(s, 0, sizeof *s); return; }
    if (s->receiving && message_ignored(state, s->wanted)) s->receiving = false;
    if (s->transmitting && message_ignored(state, s->tx)) s->transmitting = false;
    if (!send || now < s->frame_at) return;
    // One outgoing frame per 250 ms, even under hostile WANT traffic. Three WANT attempts per transfer.
    uint8_t p[27 + MSG_FRAGMENT]; static const uint8_t all[6] = {255,255,255,255,255,255};
    if (s->receiving && now >= s->retry_at) {
        if (s->attempts >= 3) {
            s->receiving = false; memcpy(s->failed, s->wanted, 16); s->cooldown = now + 15000;
        } else {
            header(p, WANT, s->wanted); send(s->peer, p, 22); s->attempts++; s->retry_at = now + 2000;
            s->frame_at = now + 250; return;
        }
    }
    if (s->transmitting) {
        int f = s->tx_fragment, n = f ? MSG_RECORD_BYTES - MSG_FRAGMENT : MSG_FRAGMENT;
        header(p, DATA, s->tx); p[22] = f;
        uint32_t checksum = message_crc(s->tx, sizeof s->tx);
        for (int i = 0; i < 4; i++) p[23+i] = checksum >> (8*i);
        memcpy(p + 27, s->tx + f * MSG_FRAGMENT, n);
        // A failed driver send is recovered by the receiver's bounded WANT retry.
        send(s->tx_peer, p, 27 + n); s->tx_fragment++;
        if (s->tx_fragment == 2) s->transmitting = false;
        s->frame_at = now + 250; return;
    }
    if (state->count && now >= s->offer_at) {
        header(p, OFFER, state->messages[s->offer++ % state->count].id); send(all, p, 22);
        s->offer_at = now + 2000; s->frame_at = now + 250;
    }
}
