#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "badge/badge.h"
#include "native_messages.h"
#include "native_messages_portal.h"

static bool active,launched;
static int32_t show_info(int);
static am_result_t begin(void *ctx)
{
    (void)ctx;
    if(active || am_msg_portal_attempted())return AM_BUSY;
    active=true;launched=false;return AM_OK;
}
static int32_t start(void *ctx)
{ (void)ctx;if(!launched){show_info(3000);launched=true;}return messaging_start(); }
static int32_t snapshot(void *ctx,message_state_t *out,uint64_t *revision)
{ (void)ctx;return messaging_snapshot(out,revision); }
static int32_t read_thread(void *ctx,const uint8_t *id) { (void)ctx;return messaging_read(id); }
static int32_t status(void *ctx,char *out,uint32_t capacity)
{
    (void)ctx;const char *s=messaging_status();uint32_t n=0;
    while(n+1<capacity && s[n]){out[n]=s[n];n++;}out[n]=0;return (int32_t)n;
}
static uint64_t utc_now(void *ctx) { (void)ctx;return am_msg_portal_now(); }
static int32_t portal_start(void *ctx,const uint8_t *id) { (void)ctx;return am_msg_portal_start(id); }
static int32_t portal_view(void *ctx,badge_msg_phone_t *out)
{
    (void)ctx;
    memcpy(out->ssid,am_msg_portal_ssid(),sizeof out->ssid);out->ssid[19]=0;
    memcpy(out->password,am_msg_portal_password(),sizeof out->password);out->password[16]=0;return 1;
}
static void info_lines(const char *s,int y,int scale,unsigned rows)
{
    unsigned cols=(W-96)/(8*(unsigned)scale);
    while(*s && rows--) {
        char line[64];unsigned n=0;
        while(s[n] && s[n]!='\n' && n<cols)n++;
        if(n==cols && s[n] && s[n]!=' '){unsigned cut=n;while(cut && s[cut]!=' ')cut--;if(cut)n=cut;}
        memcpy(line,s,n);line[n]=0;text(48,y,scale,line,true);y+=8*scale+8;s+=n;
        if(*s==' ' || *s=='\n')s++;
    }
}
static int32_t show_info(int timeout)
{
    const char *label=timeout<0?"Back":"Skip";ui_keys(label,label,label,label);box(0,TOP,W,H-TOP,false);
    text(48,TOP+40,5,"Messages",true);
    info_lines("Store up to 8 public messages in threads from your phone. Relay is optional. Sender names and phone times are unverified.",TOP+128,3,4);
    info_lines("Code: apps/messaging + components/os/messages",TOP+340,2,2);
    text(48,H-132,2,"Source: github.com/yigitabi5444/supercon26-badge-os",true);
    text(48,H-72,2,"Hold MENU + and MENU - to return home",true);
    text(48,H-40,2,timeout<0?"Any button: back":"Any button: skip",true);ui_frame(1);ui_close();
    btn_flush();btn_get(timeout);vTaskDelay(pdMS_TO_TICKS(20)?pdMS_TO_TICKS(20):1);
    while(btn_held())vTaskDelay(1);
    btn_flush();return 1; // Module redraws; no backup or private OS API.
}
static int32_t info(void *ctx) { (void)ctx;return show_info(-1); }
static am_result_t finish(void *ctx)
{
    (void)ctx;
    if(am_msg_portal_attempted())return AM_BUSY; // Original AP/DNS/HTTP have boot lifetime, including partial failure.
    active=false;return AM_OK; // Shared store/radio/unread service remains OS-owned.
}
const am_native_messages_t am_native_messages_target={.begin=begin,.start=start,.snapshot=snapshot,
    .read_thread=read_thread,.status=status,.utc_now=utc_now,.portal_start=portal_start,.portal_view=portal_view,
    .info=info,.finish=finish};
