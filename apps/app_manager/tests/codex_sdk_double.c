#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_http_client.h"
#include "driver/usb_serial_jtag.h"
#include "badge/badge.h"
#include "native_host_double.h"
#include "codex_sdk_double.h"
cb_sdk_fixture_t cb_sdk;
struct local_http_double { unsigned marker; };
static struct local_http_double client;
void cb_sdk_reset(void)
{ bool installed=cb_sdk.installed;cb_sdk=(cb_sdk_fixture_t){.status=200,.declared=-2,.installed=installed}; }
int cfg_get(const char *key,char *out,int cap)
{
    assert(cap>0);const char *s;
    if(!strcmp(key,"cb_url"))s=cb_sdk.url?cb_sdk.url:"";
    else {assert(!strcmp(key,"cb_seen"));s=cb_sdk.seen;}
    snprintf(out,(size_t)cap,"%s",s);return (int)strlen(s);
}
bool cfg_set(const char *key,const char *value)
{
    assert(!strcmp(key,"cb_seen") && strlen(value)<sizeof cb_sdk.seen);
    strcpy(cb_sdk.seen,value);cb_sdk.saves++;return true;
}
void leds(const uint8_t rgb[LEDS][3])
{
    bool on=rgb[0][0]!=0;
    for(unsigned i=0;i<LEDS;i++)assert(rgb[i][0]==(on?10:0) && !rgb[i][1] && !rgb[i][2]);
    if(on){assert(cb_sdk.seen[0] && cb_sdk.saves);cb_sdk.led_on++;}
    cb_sdk.lit=on;
}
bool wifi_connect(int timeout)
{ assert(timeout==5000 && cb_sdk.url && !cb_sdk.readies);cb_sdk.joins++;cb_sdk.radio=true;return !cb_sdk.join_fail; }
bool wifi_connected(void){return cb_sdk.radio && !cb_sdk.join_fail;}
void wifi_off(void){assert(cb_sdk.radio && !cb_sdk.clients);cb_sdk.radio=false;cb_sdk.off++;}
void ranging_start(void){assert(!cb_sdk.radio && !cb_sdk.clients);cb_sdk.ranging++;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c)
{
    assert(cb_sdk.radio && !cb_sdk.clients && c->url && !strcmp(c->url,cb_sdk.url));
    assert(c->timeout_ms==5000 && c->buffer_size==1024 && c->disable_auto_redirect);
    assert(!strcmp(c->user_agent,"codex-board") && !c->event_handler && !c->user_data);
    if(cb_sdk.init_fail)return NULL;
    cb_sdk.clients++;cb_sdk.offset=0;return &client;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c,int n)
{ assert(c==&client && !n);cb_sdk.opened++;return cb_sdk.open_fail?ESP_FAIL:ESP_OK; }
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t c,int n)
{ assert(c==&client && n>0 && n<=5000);return ESP_OK; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c)
{ assert(c==&client);return cb_sdk.declared==-2?(int64_t)strlen(cb_sdk.body):cb_sdk.declared; }
int esp_http_client_get_status_code(esp_http_client_handle_t c){assert(c==&client);return cb_sdk.status;}
int esp_http_client_read(esp_http_client_handle_t c,char *out,int cap)
{
    assert(c==&client && cap>0 && cap<=8192);cb_sdk.reads++;
    atomic_fetch_add(&nh_time_ms,cb_sdk.read_delay_ms);
    size_t n=strlen(cb_sdk.body)-cb_sdk.offset;
    if(n>(size_t)cap)n=(size_t)cap;
    if(cb_sdk.read_limit && n>cb_sdk.read_limit)n=cb_sdk.read_limit;
    memcpy(out,cb_sdk.body+cb_sdk.offset,n);cb_sdk.offset+=n;return (int)n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c)
{ assert(c==&client);return !cb_sdk.incomplete && (cb_sdk.complete_on_headers || cb_sdk.offset==strlen(cb_sdk.body)); }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c)
{ assert(c==&client && cb_sdk.clients==1);cb_sdk.clients--;cb_sdk.cleaned++;return ESP_OK; }
bool usb_serial_jtag_is_driver_installed(void){return cb_sdk.installed;}
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *c)
{ assert(c->rx_buffer_size==8448 && c->tx_buffer_size==256 && !cb_sdk.installed);cb_sdk.installed=true;cb_sdk.installs++;return ESP_OK; }
void usb_serial_jtag_vfs_use_driver(void){assert(cb_sdk.installed && !cb_sdk.radio);}
int usb_serial_jtag_read_bytes(void *out,uint32_t cap,uint32_t wait)
{
    assert(cb_sdk.installed && !cb_sdk.radio && !wait && cap>0 && cap<=256);
    const char *input=cb_sdk.usb_input?cb_sdk.usb_input:"";size_t n=strlen(input)-cb_sdk.usb_offset;
    if(n>7)n=7; // Exercise frame parsing across actual SDK read boundaries.
    if(n>cap)n=cap;
    memcpy(out,input+cb_sdk.usb_offset,n);cb_sdk.usb_offset+=n;return (int)n;
}
int usb_serial_jtag_write_bytes(const void *out,size_t n,uint32_t wait)
{
    assert(cb_sdk.installed && !cb_sdk.radio && !wait);
    static const char ack[]="CODEXBOARD ACK abc\n",ready[]="\nCODEXBOARD READY\n";
    if(n==sizeof ack-1 && !memcmp(out,ack,n))cb_sdk.acks++;
    else {assert(n==sizeof ready-1 && !memcmp(out,ready,n));cb_sdk.readies++;}
    return (int)n;
}
