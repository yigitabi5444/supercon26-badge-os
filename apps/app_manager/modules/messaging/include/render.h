// Private artifact interface, not a host capability or shared ABI. GPL-2.0.
#pragma once
#include "controller.h"
#include "badge_native_api.h"
typedef struct { char ssid[20], password[17]; } msg_phone_view_t;
// Values are copied; no module string pointers or callbacks escape the call.
typedef struct { char key[4][21]; } msg_labels_t;
enum { MSG_RENDER_INVALID=-1, MSG_RENDER_BUSY=0, MSG_RENDER_OK=1 };
// Consumes caller-owned snapshots synchronously. No task, allocation or retained pointer.
// A successful call pairs begin/clear/draw/submit/frame_idle. Busy leaves labels untouched.
// Standalone entry supplies a private submit shim to copy labels through typed keys.
int msg_render(const badge_native_api_t *api,const message_state_t *state,
               const msg_view_t *view,bool available,int read_result,uint64_t utc_now,
               const msg_phone_view_t *phone,const char *status,msg_labels_t *labels);
// Private two-phase form: validate before taking the host lease, then perform
// read intent only after begin succeeds. owned requires the validated same inputs
// and a held lease; it always submits and drains before returning.
bool msg_render_valid(const badge_native_api_t *,const message_state_t *,const msg_view_t *,bool,
                      const msg_phone_view_t *,const char *,msg_labels_t *);
void msg_render_owned(const badge_native_api_t *,const message_state_t *,const msg_view_t *,bool,int,
                      uint64_t,const msg_phone_view_t *,const char *,msg_labels_t *);
void msg_visible_ages(const message_state_t *,const msg_view_t *,bool,uint64_t,char [3][64]);
