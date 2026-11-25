// Portable Messages controller extracted from Badge OS9b73810; GPL-2.0.
// Groundwork only: no ELF, renderer, broker, ABI extension or install claim.
#pragma once
#include "badge/messaging.h"
typedef struct {
    uint8_t selected[16], active[16], anchor[16], known[MSG_CAP][16];
    int known_count;
    int index, offset;
    bool opened, phone_menu, phone_attempted, phone_ok, acknowledge;
} msg_view_t;
enum { MSG_KEY_PLUS, MSG_KEY_MINUS, MSG_KEY_A, MSG_KEY_B };
typedef struct {
    bool repaint, exit, info, phone, phone_has_thread, phone_sets_status;
    uint8_t thread[16];
} msg_actions_t;
bool msg_view_sync(const message_state_t *s,msg_view_t *v);
int msg_view_visible(const message_state_t *s,const msg_view_t *v,int out[3]);
// Call only for an actual dirty-frame decision, matching the native reader.
bool msg_view_frame(msg_view_t *v,bool available,uint8_t read_thread[16]);
// Returns copied intents. The caller executes synchronously before painting/next key.
void msg_view_key(msg_view_t *v,const message_state_t *s,bool available,int key,msg_actions_t *out);
void msg_view_phone_result(msg_view_t *v,const msg_actions_t *action,bool success);
