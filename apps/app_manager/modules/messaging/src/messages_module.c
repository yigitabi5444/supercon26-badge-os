// Standalone Messages entry. Host owns the original store, radio and phone portal.
// GPL-2.0. No guest callback is passed to or retained by the host.
#include "badge_messages_api.h"
#include "render.h"
#define MESSAGES_VERSION "1.0.0"
#ifndef MESSAGES_HOST_TEST
BADGE_NATIVE_INFO("messaging",MESSAGES_VERSION);
#endif
_Static_assert(sizeof(msg_phone_view_t)==sizeof(badge_msg_phone_t),"Copied phone view");
_Static_assert(sizeof(((msg_labels_t *)0)->key[0])==BADGE_MSG_LABEL_MAX+1,"Copied keys");
static struct {
    const badge_messages_api_t *host;
    badge_native_api_t draw;
    message_state_t state;
    msg_view_t view;
    msg_phone_view_t phone;
    badge_msg_phone_t copied_phone;
    msg_labels_t labels;
    char status[BADGE_MSG_STATUS_MAX],painted_ages[3][64];
    bool portal_attempted;
} session;
static void zero(void *data,size_t n) {uint8_t *p=data;for(size_t i=0;i<n;i++)p[i]=0;}
static void copy(void *out,const void *in,size_t n) {uint8_t *d=out;const uint8_t *s=in;for(size_t i=0;i<n;i++)d[i]=s[i];}
static bool equal(const void *one,const void *two,size_t n) {const uint8_t *a=one,*b=two;for(size_t i=0;i<n;i++)if(a[i]!=b[i])return false;return true;}
static bool nul(const char *s,size_t n) {for(size_t i=0;i<n;i++)if(!s[i])return true;return false;}
static bool api_valid(const badge_native_api_t *base)
{
    if(!base || base->abi_version!=BADGE_NATIVE_ABI || base->struct_size<sizeof(badge_messages_api_t) ||
       base->width!=960 || base->height!=672 || base->top!=48 || !base->begin_frame || !base->submit_frame ||
       !base->clear || !base->box || !base->outline || !base->text || !base->frame_idle)return false;
    const badge_messages_api_t *a=(const badge_messages_api_t *)base;
    return a->start && a->key && a->keys && a->snapshot && a->read_thread && a->status && a->utc_now &&
           a->portal_start && a->portal_view && a->info && a->release;
}
static void submit(void *context)
{
    // Called only by the synchronous guest renderer, while the host lease is held.
    const badge_messages_api_t *a=session.host;
    a->keys(context,session.labels.key[0],session.labels.key[1],session.labels.key[2],session.labels.key[3]);
    a->base.submit_frame(context);
}
static void status_copy(void)
{
    const badge_messages_api_t *a=session.host;zero(session.status,sizeof session.status);
    if(a->status(a->base.context,session.status,sizeof session.status)<0 || !nul(session.status,sizeof session.status)) {
        static const char fallback[]="Message status unavailable. Use Home.";
        zero(session.status,sizeof session.status);copy(session.status,fallback,sizeof fallback);
    }
}
static void phone(const msg_actions_t *action)
{
    const badge_messages_api_t *a=session.host;
    session.portal_attempted=true; // Includes every partial or failed setup attempt.
    bool ok=a->portal_start(a->base.context,action->phone_has_thread?action->thread:0)==1;
    if(ok) {
        ok=a->portal_view(a->base.context,&session.copied_phone)==1 &&
           nul(session.copied_phone.ssid,sizeof session.copied_phone.ssid) &&
           nul(session.copied_phone.password,sizeof session.copied_phone.password);
        if(ok){copy(session.phone.ssid,session.copied_phone.ssid,sizeof session.phone.ssid);copy(session.phone.password,session.copied_phone.password,sizeof session.phone.password);}
    }
    msg_view_phone_result(&session.view,action,ok);
}
__attribute__((visibility("default"),aligned(4)))
int app_main(int argc,char **argv)
{
    (void)argc;(void)argv;
    const badge_native_api_t *base=badge_app_get_api(BADGE_NATIVE_ABI,sizeof(badge_messages_api_t));
    if(!api_valid(base))return -1;
    zero(&session,sizeof session);session.host=(const badge_messages_api_t *)base;
    session.draw=*base;session.draw.submit_frame=submit; // Private table; never given to host.
    const badge_messages_api_t *a=session.host;void *context=base->context;
    a->start(context);bool dirty=true,was_available=false;uint64_t last_revision=UINT64_MAX;
    for(;;) {
        uint64_t revision=last_revision;bool available=a->snapshot(context,&session.state,&revision)==1;
        if(available && !msg_view_sync(&session.state,&session.view))available=false;
        if(revision!=last_revision || available!=was_available)dirty=true;
        last_revision=revision;was_available=available;
        uint64_t now=a->utc_now(context);char ages[3][64];
        msg_visible_ages(&session.state,&session.view,available,now,ages);
        if(!equal(ages,session.painted_ages,sizeof ages))dirty=true;
        if(dirty) {
            if(!available)status_copy();
            // Preserve native read intent, but never mark an unseen busy frame read.
            if(msg_render_valid(&session.draw,&session.state,&session.view,available,&session.phone,session.status,&session.labels) && base->begin_frame(context)) {
                uint8_t thread[16];int result=MSG_DUP;
                if(msg_view_frame(&session.view,available,thread))result=a->read_thread(context,thread);
                msg_render_owned(&session.draw,&session.state,&session.view,available,result,now,&session.phone,session.status,&session.labels);
                copy(session.painted_ages,ages,sizeof ages);dirty=false;
            }
        }
        int key=a->key(context,1000);if(key<BADGE_MSG_PLUS || key>BADGE_MSG_B)continue;
        msg_actions_t action;msg_view_key(&session.view,&session.state,available,key,&action);
        if(action.repaint)dirty=true;
        if(action.info){a->info(context);dirty=true;}
        if(action.phone)phone(&action);
        if(action.exit) {
            // The host is authoritative even if a prior attempt is not reflected
            // in this view. No normal return after any actual portal_start call.
            if(!session.portal_attempted && a->release(context)==1)return 0;
            session.view.phone_attempted=true;dirty=true;
        }
    }
}
