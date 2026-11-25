// Trusted Codex Board I/O. Every call completes before module memory can unload.
#include <stdio.h>
#include <string.h>
#include "esp_http_client.h"
#include "esp_timer.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "badge/badge.h"
#include "native_codex.h"

static struct { char url[241];uint32_t mode;bool active,radio,usb; } receiver;
static bool url_allowed(const char *url)
{
    if(strncmp(url,"http://",7) || !url[7] || url[7]=='/')return false;
    for(const unsigned char *p=(const unsigned char *)url;*p;p++)
        if(*p<=32 || *p>126 || *p=='@' || *p=='#')return false;
    return true;
}
static void red(void *ctx,uint32_t lit)
{
    (void)ctx;
    static const uint8_t on[LEDS][3]={{10},{10},{10},{10},{10},{10},{10},{10}};
    static const uint8_t off[LEDS][3]={0};
    leds(lit?on:off);
}
static am_result_t begin(void *ctx)
{
    if(receiver.active)return AM_BUSY;
    memset(&receiver,0,sizeof receiver);
    cfg_get("cb_url",receiver.url,sizeof receiver.url);
    receiver.url[sizeof receiver.url-1]=0;
    receiver.mode=!receiver.url[0]?BADGE_CODEX_USB:url_allowed(receiver.url)?BADGE_CODEX_LAN:BADGE_CODEX_INVALID;
    receiver.active=true;red(ctx,0);return AM_OK;
}
static uint32_t mode(void *ctx) { (void)ctx;return receiver.mode; }
static int32_t seen_load(void *ctx,char *out,uint32_t cap)
{ (void)ctx;return cfg_get("cb_seen",out,(int)cap)>=0; }
static int32_t seen_save(void *ctx,const char *value)
{ (void)ctx;return cfg_set("cb_seen",value); }
static int32_t join(void *ctx)
{
    (void)ctx;
    if(!receiver.active || receiver.mode!=BADGE_CODEX_LAN)return 0;
    receiver.radio=true;return wifi_connect(5000);
}
static bool budget(esp_http_client_handle_t client,int64_t deadline)
{
    int64_t left=deadline-esp_timer_get_time();
    return left>0 && esp_http_client_set_timeout_ms(client,(int)((left+999)/1000))==ESP_OK;
}
static int32_t fetch(void *ctx,char *out,uint32_t cap)
{
    (void)ctx;
    if(!receiver.active || receiver.mode!=BADGE_CODEX_LAN || !receiver.radio || !wifi_connected())
        return BADGE_CODEX_FETCH_OFFLINE;
    if(cap!=BADGE_CODEX_BODY_MAX)return BADGE_CODEX_FETCH_INCOMPLETE;
    esp_http_client_handle_t client=esp_http_client_init(&(esp_http_client_config_t){
        .url=receiver.url,.timeout_ms=5000,.buffer_size=1024,
        .user_agent="codex-board",.disable_auto_redirect=true});
    if(!client)return BADGE_CODEX_FETCH_INIT;
    int32_t result=BADGE_CODEX_FETCH_OFFLINE;uint32_t count=0;
    int64_t deadline=esp_timer_get_time()+5000000LL;
    if(esp_http_client_open(client,0)!=ESP_OK || !budget(client,deadline))goto done;
    int64_t declared=esp_http_client_fetch_headers(client);
    if(declared<0 || !budget(client,deadline))goto done;
    if(esp_http_client_get_status_code(client)!=200){result=BADGE_CODEX_FETCH_HTTP;goto done;}
    if(declared>BADGE_CODEX_BODY_MAX){result=BADGE_CODEX_FETCH_TOO_BIG;goto done;}
    result=BADGE_CODEX_FETCH_INCOMPLETE;
    // Headers may buffer the whole body: drain read before checking completion.
    for(;;) {
        if(!budget(client,deadline))goto done;
        char extra;int n=esp_http_client_read(client,count<cap?out+count:&extra,(int)(count<cap?cap-count:1));
        if(n<0 || (uint32_t)n>(count<cap?cap-count:1))goto done;
        if(count==cap && n){result=BADGE_CODEX_FETCH_TOO_BIG;goto done;}
        count+=(uint32_t)n;
        if(!budget(client,deadline))goto done;
        if(esp_http_client_is_complete_data_received(client)){result=(int32_t)count;break;}
        if(!n)break;
    }
done:
    esp_http_client_cleanup(client);return result;
}
static int32_t usb_begin(void *ctx)
{
    (void)ctx;
    if(!receiver.active || receiver.mode!=BADGE_CODEX_USB)return 0;
    if(!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config={.rx_buffer_size=8448,.tx_buffer_size=256};
        if(usb_serial_jtag_driver_install(&config)!=ESP_OK)return 0;
    }
    // The SDK ISR and copied ring buffers stay host-resident on their install core.
    usb_serial_jtag_vfs_use_driver();receiver.usb=true;
    static const char ready[]="\nCODEXBOARD READY\n";
    usb_serial_jtag_write_bytes(ready,sizeof ready-1,0);return 1;
}
static int32_t usb_read(void *ctx,char *out,uint32_t cap)
{
    (void)ctx;
    if(!receiver.usb)return 0;
    int n=usb_serial_jtag_read_bytes(out,cap,0);return n>0?n:0;
}
static int32_t usb_ack(void *ctx,const char *revision)
{
    (void)ctx;if(!receiver.usb)return 0;
    char line[50];int n=snprintf(line,sizeof line,"CODEXBOARD ACK %.32s\n",revision);
    return usb_serial_jtag_write_bytes(line,(size_t)n,0)==n;
}
static void release(void *ctx)
{
    if(!receiver.active)return;
    red(ctx,0);
    if(receiver.radio){wifi_off();ranging_start();}
    memset(&receiver,0,sizeof receiver); // No endpoint, module pointer or callback survives.
}
const am_native_codex_t am_native_codex_target={.begin=begin,.mode=mode,.red_leds=red,
    .seen_load=seen_load,.seen_save=seen_save,.lan_join=join,.lan_fetch=fetch,
    .usb_begin=usb_begin,.usb_read=usb_read,.usb_ack=usb_ack,.release=release};
