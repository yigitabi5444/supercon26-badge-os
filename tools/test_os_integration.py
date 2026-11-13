#!/usr/bin/env python3
"""Exercise the actual v1.2 OS port with mocked IDF/panel I/O, never a device.
Run python3 tools/test_os_integration.py (C compiler + ASan/UBSan required).
Panel timings below are deterministic test fixtures, not measurements.
"""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
out=Path(tempfile.mkdtemp(prefix='badge-os-v12-'))
def source(path):
    return '\n'.join(l for l in (root/path).read_text().splitlines() if not l.startswith('#include'))
preamble=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>
#include "badge/badge.h"
#include "badge/os.h"
#include "badge/font8x8_basic.h"
#define EXT_RAM_BSS_ATTR
#define portMAX_DELAY -1
#define pdMS_TO_TICKS(x) (x)
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 1
#define ESP_ERR_NVS_NO_FREE_PAGES 2
#define ESP_ERR_NVS_NEW_VERSION_FOUND 3
#define ESP_EVENT_ANY_ID -1
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_STA 0
#define WIFI_IF_STA 0
#define WIFI_PS_NONE 0
#define EPD_CALM 0
#define EPD_FAST 1
#define ESP_ERROR_CHECK(x) assert((x)==ESP_OK)
#define ESP_LOGD(tag,...) do { (void)(tag); if(0) fprintf(stderr,__VA_ARGS__); } while(0)
#define ESP_LOGE ESP_LOGD
typedef int SemaphoreHandle_t;
typedef unsigned *EventGroupHandle_t;
typedef const char *esp_event_base_t;
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef struct { int nvs_enable; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() {.nvs_enable=1}
typedef struct { struct { unsigned char ssid[32],password[64]; } sta; } wifi_config_t;
enum { IP_EVENT_STA_GOT_IP=1, IP_EVENT_STA_LOST_IP, WIFI_EVENT_STA_START,
       WIFI_EVENT_STA_DISCONNECTED, WIFI_EVENT_STA_STOP, WIFI_EVENT_AP_STACONNECTED };
enum { NVS_READONLY, NVS_READWRITE };
static const char *IP_EVENT="ip",*WIFI_EVENT="wifi";
static int mutex_depth, frames, closes, resets, wait_calls, wipes, connects, stops, disconnects;
static int64_t clock_us, release_at, dwell_started;
static bool active_pull, variant84, callback_frame, callback_cancel;
static int held_mask, flushes, last_timeout, boot_error, deinit_calls, nvs_reads, nvs_writes, flash_erases;
static int init_nvs=-1, svc_started, range_started, phase, unread;
static unsigned bits;
static volatile bool dismiss_card;
static uint8_t output[PLANE];
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return 1; }
static int xSemaphoreTake(int x,int timeout) { (void)x;(void)timeout;assert(!mutex_depth);mutex_depth=1;return 1; }
static void xSemaphoreGive(int x) { (void)x;assert(mutex_depth==1);mutex_depth=0; }
static EventGroupHandle_t xEventGroupCreate(void) { return &bits; }
static unsigned xEventGroupSetBits(EventGroupHandle_t e,unsigned b) { return *e|=b; }
static unsigned xEventGroupClearBits(EventGroupHandle_t e,unsigned b) { return *e&=~b; }
static unsigned xEventGroupGetBits(EventGroupHandle_t e) { return *e; }
static unsigned xEventGroupWaitBits(EventGroupHandle_t e,unsigned b,bool c,bool a,int t)
{ (void)c;(void)a;(void)t;return *e&b; }
static int64_t esp_timer_get_time(void) { return clock_us; }
static void epd_init(void) {}
static bool epd_is84(void) { return variant84; }
static void epd_wait(void) { assert(mutex_depth);wait_calls++;active_pull=false;clock_us+=240000; }
static void epd_close(void) { assert(mutex_depth&&!active_pull);closes++;clock_us+=200000; }
static void epd_reset(void) { assert(mutex_depth&&!active_pull);resets++; }
static void epd_load(const uint8_t*a,const uint8_t*b) { (void)a;(void)b;assert(mutex_depth&&!active_pull); }
static void epd_start(int mode) { assert(mutex_depth&&!active_pull);if(mode==EPD_FAST)wipes++; }
static void epd_frame(const uint8_t*a,const uint8_t*b)
{ (void)b;assert(mutex_depth&&!active_pull);memcpy(output,a,PLANE);frames++;active_pull=true; }
static void epd_pull(int t) { (void)t;assert(mutex_depth&&!active_pull); }
typedef struct { const char *version; } desc_t;
static const desc_t *esp_app_get_description(void) { static desc_t d={"1.2"};return &d; }
int battery_pct(void) { return 100; }
static int cricket_version(void) { return 1; }
static void esp_register_shutdown_handler(void(*f)(void)) { (void)f; }
static int esp_netif_init(void) { return 0; }
static int esp_event_loop_create_default(void) { return 0; }
static void esp_netif_create_default_wifi_sta(void) {}
static int esp_wifi_init(const wifi_init_config_t*c) { init_nvs=c->nvs_enable;return 0; }
static int esp_wifi_set_storage(int n) { (void)n;return 0; }
static int esp_wifi_set_mode(int n) { (void)n;return 0; }
static int esp_wifi_start(void) { return 0; }
static int esp_wifi_set_config(int n,const wifi_config_t*c) { (void)n;(void)c;return 0; }
static void esp_wifi_set_ps(int n) { (void)n; }
static int esp_wifi_connect(void) { connects++;return 0; }
static void esp_wifi_disconnect(void) { disconnects++; }
static void esp_wifi_stop(void) { stops++; }
static void esp_event_handler_register(esp_event_base_t b,int id,void(*f)(void*,esp_event_base_t,int32_t,void*),void*a)
{ (void)b;(void)id;(void)f;(void)a; }
void ranging_stop(void) {}
void ranging_start(void) { range_started=++phase; }
int messaging_unread(void) { return unread; }
bool messaging_start(void) { svc_started=++phase;return false; }
static int nvs_open(const char*n,int mode,nvs_handle_t*h) { (void)n;(void)mode;*h=1;nvs_reads++;return 0; }
static int nvs_get_str(int h,const char*k,char*out,size_t*n) { (void)h;(void)k;(void)out;(void)n;return 1; }
static void nvs_close(int h) { (void)h; }
static int nvs_set_str(int h,const char*k,const char*v) { (void)h;(void)k;(void)v;nvs_writes++;return 0; }
static int nvs_erase_key(int h,const char*k) { (void)h;(void)k;nvs_writes++;return 0; }
static int nvs_commit(int h) { (void)h;return 0; }
static int esp_flash_read(void*f,void*out,unsigned a,size_t n) { (void)f;(void)a;memset(out,0,n);return 0; }
static int esp_flash_erase_region(void*f,unsigned a,size_t n) { (void)f;(void)a;(void)n;flash_erases++;return 0; }
static int esp_flash_write(void*f,const void*b,unsigned a,size_t n) { (void)f;(void)b;(void)a;(void)n;return 0; }
static int nvs_flash_init(void) { return boot_error; }
static void nvs_flash_deinit(void) { deinit_calls++; }
static const char *esp_err_to_name(int e) { (void)e;return "fixture"; }
void sao_init(void) {}
void buttons_init(void) {}
void leds_init(void) {}
static void home(void) {}
void ms(int t) { clock_us+=t*1000;if(clock_us>=release_at)held_mask=0; }
int btn_held(void) { return held_mask; }
void btn_flush(void) { flushes++; }
int btn_get(int timeout) {
 assert(!mutex_depth&&!active_pull);last_timeout=timeout;dwell_started=clock_us;
 if(callback_frame) {
  callback_frame=false;int n=frames,w=wipes;uint8_t saved[PLANE];memcpy(saved,output,PLANE);
  memset(fb+TOP*W/8,0x5a,PLANE-TOP*W/8);ui_frame(1);ui_wipe();ui_close();
  assert(frames==n&&wipes==w&&!memcmp(saved,output,PLANE));
 }
 if(callback_cancel) { callback_cancel=false;dismiss_card=true;return -1; }
 return timeout<0 ? BTN_B : -1;
}
'''
checks=r'''
int main(void) {
 display_init();cfg_storage_init(false);
 assert(!cfg_storage_ready()&&!cfg_set("name","demo")&&!cfg_set_image(NULL));
 assert(!nvs_reads&&!nvs_writes&&!flash_erases);
 char b[16]="unchanged";assert(cfg_get("name",b,sizeof b)==-1&&!b[0]&&!nvs_reads);
 wifi_on();assert(init_nvs==0);int f=frames;wifi_join=false;
 on_wifi(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);assert(!wifi_connected());
 wifi_join=true;on_wifi(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);assert(wifi_connected());
 on_wifi(NULL,IP_EVENT,IP_EVENT_STA_LOST_IP,NULL);assert(!wifi_connected());
 on_wifi(NULL,WIFI_EVENT,WIFI_EVENT_AP_STACONNECTED,NULL);assert(!wifi_connected());
 on_wifi(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);on_wifi(NULL,WIFI_EVENT,WIFI_EVENT_STA_STOP,NULL);
 assert(!wifi_connected());on_wifi(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);wifi_off();
 assert(!wifi_connected()&&disconnects==1&&stops==1&&frames==f);
 cfg_storage_init(true);assert(cfg_set("name","DEMO")&&nvs_writes==1);
 wifi_ev=NULL;wifi_on();assert(init_nvs==1);
 const app_t app={.name="Update"};
 const app_info_t help={.info="A compact test description.",.credit="Code: apps/update/",.launch_info=true};
 os_app(&app,&help);ui_keys("Up","Open","Down","Phone");memset(fb,0,PLANE);ui_frame(1);
 assert(active_pull);ui_close();assert(!active_pull);int count=frames;ui_frame(1);assert(frames==count);
 callback_frame=true;held_mask=1;release_at=clock_us+600000;int old_flush=flushes;
 ui_info();assert(!info_visible&&!mutex_depth&&!active_pull&&flushes==old_flush+1);
 assert(fb[TOP*W/8]==0x5a&&!memcmp(fb,shown,PLANE)&&!held_mask);
 assert(!strcmp(keys[BTN_PLUS],"Up")&&!strcmp(keys[BTN_B],"Phone"));
 os_launch_info();assert(last_timeout==2800&&!active_pull&&!info_visible);
 dismiss_card=false;callback_cancel=true;ui_info_until(&dismiss_card);assert(dismiss_card&&!info_visible);
 count=frames;ui_info_until(&dismiss_card);assert(frames==count);
 os_app(NULL,NULL);ui_frame(1);assert(!in_app);ui_close();
 variant84=true;pulls=20;ui_frame(1); /* unchanged frame does not reopen */
 fb[TOP*W/8+50]^=1;int n=resets;ui_frame(1);assert(resets==n+1);ui_close();
 for(int e=ESP_ERR_NVS_NO_FREE_PAGES;e<=ESP_ERR_NVS_NEW_VERSION_FOUND;e++) {
  boot_error=e;int old=deinit_calls;phase=0;app_main();
  assert(!cfg_storage_ready()&&deinit_calls==old+1&&svc_started<range_started);
 }
 boot_error=0;app_main();assert(cfg_storage_ready()&&svc_started<range_started);
 assert(wait_calls>0&&closes>0&&!mutex_depth);puts("OS v1.2: Info/background suppression/restore/dwell, IP lifecycle, NVS guard and boot order PASS");
}
'''
code=preamble+'\n'+source('components/os/settings.c')+'\n'+source('components/os/wifi.c')+'\n'+source('components/os/draw.c')+'\n'+source('components/os/display.c')+'\n'+source('main/main.c')+'\n'+checks
(out/'check.c').write_text(code)
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-I',str(root/'components/os/include'),str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],check=True)
