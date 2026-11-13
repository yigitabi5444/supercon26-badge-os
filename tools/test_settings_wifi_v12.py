#!/usr/bin/env python3
"""Actual Settings scan/HTTP code + shipped page JS. No radio/device/NVS access.
Run python3 tools/test_settings_wifi_v12.py; requires cc (ASan/UBSan) and node.
"""
from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'apps/settings/settings.c').read_text().replace('#include "settings_networks.h"',(ROOT/'apps/settings/settings_networks.h').read_text())
out=Path(tempfile.mkdtemp(prefix='settings-wifi-'))
page=(ROOT/'apps/settings/settings_page.h').read_text()
code=s[s.index('static void json_str'):s.index('static esp_err_t get_page')]
startup=s[s.index('    if (scan_abandoned && network_task)'):s.index('    xTaskCreate(dns_task,')]
code+='\nstatic void startup_cleanup(void) {\n'+startup+'}\n'
pre=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_TIMEOUT 3
#define HTTPD_400_BAD_REQUEST 400
#define MALLOC_CAP_SPIRAM 0
#define WIFI_SCAN_TYPE_ACTIVE 0
#define portMAX_DELAY -1
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
typedef int esp_err_t;
typedef int SemaphoreHandle_t;
typedef int TaskHandle_t;
typedef const char *esp_event_base_t;
typedef struct { uint8_t ssid[33]; int8_t rssi; } wifi_ap_record_t;
typedef struct { unsigned status; } wifi_event_sta_scan_done_t;
typedef struct { uint8_t schan,nchan; } wifi_country_t;
typedef struct { int channel,scan_type; struct { struct { int min,max; } active; } scan_time; int home_chan_dwell_time; } wifi_scan_config_t;
typedef struct { int content_len,status; } httpd_req_t;
static int lock_depth,done_signal,scan_calls,records_calls,clears,stops,notifies,live_allocs,fail,channel;
static int driver_results; // model driver-owned list independently of application heap
static int64_t now=1000000;
static char response[4096];
static jmp_buf worker_exit;
static int worker_steps;
static void network_scan_done(void*,esp_event_base_t,int32_t,void*);
static int64_t esp_timer_get_time(void) { return now; }
static void ms(int n) { assert(!lock_depth); now+=n*1000LL; }
static int xSemaphoreTake(int sem,int ticks) { if(sem==1){assert(lock_depth++==0);return 1;}if(done_signal){done_signal=0;return 1;} if(ticks>0)now+=ticks*1000LL;return 0; }
static int xSemaphoreGive(int sem) { if(sem==1)assert(--lock_depth==0);else done_signal=1;return 1; }
static void xTaskNotifyGive(int task) { assert(!lock_depth);notifies++; }
static int ulTaskNotifyTake(int clear,int ticks) { if(!worker_steps++)return 1;longjmp(worker_exit,1); }
static void *heap_caps_malloc(size_t n,int flags) { if(fail==5)return NULL;void*p=malloc(n);if(p)live_allocs++;return p; }
static void tracked_free(void *p) { if(p)live_allocs--;free(p); }
#define free tracked_free
static int esp_wifi_get_country(wifi_country_t *c) { c->schan=1;c->nchan=3;return fail==6?ESP_FAIL:ESP_OK; }
static int esp_wifi_scan_start(const wifi_scan_config_t *c,bool block) {
 assert(!block && !lock_depth && c->channel>=1 && c->channel<=3);
 assert(c->scan_time.active.max==40 && c->home_chan_dwell_time==80);
 scan_calls++;channel=c->channel;if(fail==1)return ESP_FAIL;
 if(fail!=2){driver_results=2;wifi_event_sta_scan_done_t d={.status=fail==3};network_scan_done(NULL,NULL,0,&d);}return ESP_OK;
}
static int esp_wifi_scan_get_ap_records(uint16_t *n,wifi_ap_record_t *r) {
 assert(!lock_depth && *n==16);records_calls++;if(fail==4)return ESP_FAIL;
 driver_results=0;*n=2;memset(r,0,*n*sizeof*r);strcpy((char*)r[0].ssid,"Shared");r[0].rssi=-30;
 snprintf((char*)r[1].ssid,33,"Channel %d",channel);r[1].rssi=-40;return ESP_OK;
}
static int esp_wifi_scan_stop(void) { assert(!lock_depth);stops++;return ESP_OK; }
static int esp_wifi_clear_ap_list(void) { assert(!lock_depth);clears++;driver_results=0;return ESP_OK; }
static void httpd_resp_set_status(httpd_req_t*r,const char*s) { r->status=atoi(s); }
static void httpd_resp_set_type(httpd_req_t*r,const char*s) {}
static void httpd_resp_set_hdr(httpd_req_t*r,const char*k,const char*v) { assert(!strcmp(v,"no-store")); }
static int httpd_resp_send(httpd_req_t*r,const char*b,int n) { assert(!lock_depth && n<sizeof response);memcpy(response,b,n);response[n]=0;return 0; }
static int httpd_resp_send_err(httpd_req_t*r,int code,const char*s) { r->status=code;return -1; }
static int httpd_resp_send_500(httpd_req_t*r) { r->status=500;return -1; }
'''
tests=r'''
int main(int argc,char**argv) {
 network_lock=1;scan_done=2;network_task=1;
 wifi_ap_record_t ap={0};int n=0,omitted=0;network_t list[NET_MAX];
 memset(ap.ssid,'X',32);ap.rssi=-80;network_add(list,&n,&ap,&omitted);assert(n==1&&strlen(list[0].ssid)==32);
 ap.rssi=-20;network_add(list,&n,&ap,&omitted);assert(n==1&&list[0].rssi==-20);
 strcpy((char*)ap.ssid,"İİİİİİİİİİİİİİİİ");network_add(list,&n,&ap,&omitted);assert(n==2&&strlen(list[1].ssid)==32);
 strcpy((char*)ap.ssid,"<script>\"\\");network_add(list,&n,&ap,&omitted);assert(n==3);
 ap.ssid[0]=0;network_add(list,&n,&ap,&omitted);assert(n==3);
 const unsigned char bad[][5]={{0xc0,0xaf,0},{0xed,0xa0,0x80,0},{0xf4,0x90,0x80,0x80,0},{0xc3,0}};
 for(int i=0;i<4;i++){memcpy(ap.ssid,bad[i],5);network_add(list,&n,&ap,&omitted);}assert(n==3&&omitted==4);
 strcpy((char*)ap.ssid,"CR\rLF\nTab\t");network_add(list,&n,&ap,&omitted);assert(n==3&&omitted==5);
 for(int i=0;i<40;i++){snprintf((char*)ap.ssid,33,"AP %d",i);ap.rssi=-90+i;network_add(list,&n,&ap,&omitted);}assert(n==16);
 network_scan();assert(!network_error&&!network_busy&&network_count==4&&scan_calls==3&&records_calls==3&&live_allocs==0);
 httpd_req_t req={0};get_networks(&req);assert(!notifies&&strstr(response,"Shared")&&live_allocs==0);
 for(int f=1;f<=6;f++) { network_t old[NET_MAX];memcpy(old,networks,sizeof old);int64_t updated=network_updated;
   network_usable=true;scan_abandoned=false;fail=f;network_busy=true;network_scan();assert(network_error&&!network_busy&&!memcmp(old,networks,sizeof old)&&network_updated==updated&&live_allocs==0);
   if(f==2) { assert(!network_usable);driver_results=3;wifi_event_sta_scan_done_t late={0};network_scan_done(NULL,NULL,0,&late);
     now+=NET_COOLDOWN_US;req.status=200;scan_networks(&req);assert(req.status==503&&notifies==1&&!network_busy);
     int old_calls=scan_calls;worker_steps=0;if(!setjmp(worker_exit))network_worker(NULL);
     assert(scan_calls==old_calls&&clears==3&&!driver_results&&!memcmp(old,networks,sizeof old));
     for(int i=0;i<3;i++){now+=NET_COOLDOWN_US;scan_networks(&req);assert(req.status==503&&notifies==1&&scan_calls==old_calls);}
   }
 }assert(clears==5&&stops==1);network_usable=true;scan_abandoned=false;notifies=0;fail=0;
 now+=NET_COOLDOWN_US;req.status=200;scan_networks(&req);assert(notifies==1&&network_busy);
 req.status=200;scan_networks(&req);assert(req.status==429&&notifies==1);
 network_scan();req.status=200;scan_networks(&req);assert(req.status==429&&notifies==1);
 req.content_len=1;scan_networks(&req);assert(req.status==400);req.content_len=0;
 network_task=0;scan_networks(&req);assert(req.status==503);
 // Initial timeout before worker creation, then a completion arriving while its handle is NULL.
 network_t old[NET_MAX];memcpy(old,networks,sizeof old);int64_t updated=network_updated;
 network_task=0;network_usable=true;scan_abandoned=false;network_busy=true;fail=2;
 network_scan();assert(scan_abandoned&&!network_usable&&!network_busy&&!driver_results);
 int old_calls=scan_calls,old_notifies=notifies;wifi_event_sta_scan_done_t late={0};
 driver_results=3;network_scan_done(NULL,NULL,0,&late);
 assert(!driver_results&&notifies==old_notifies&&!memcmp(old,networks,sizeof old)&&network_updated==updated);
 // Successful later creation: execute the actual startup catch-up line, then one real worker iteration.
 network_task=1;startup_cleanup();assert(notifies==old_notifies+1);
 worker_steps=0;if(!setjmp(worker_exit))network_worker(NULL);
 assert(!driver_results&&scan_calls==old_calls&&!network_usable);
 // Creation failure: no handle ever appears; every late list is freed in the callback, never rescanned.
 network_task=0;old_notifies=notifies;
 for(int i=0;i<3;i++){driver_results=3;network_scan_done(NULL,NULL,0,&late);now+=NET_COOLDOWN_US;req.status=200;scan_networks(&req);
   assert(req.status==503&&!driver_results&&notifies==old_notifies&&scan_calls==old_calls&&!network_busy&&!memcmp(old,networks,sizeof old));}
 assert(network_updated==updated&&live_allocs==0);fail=0;
 puts("PASS: initial timeout -> late completion with NULL task -> actual startup catch-up; worker-creation failure -> repeated late lists reclaimed with no rescan/cache change");
 // Worst-case escaped byte strings and UTF-8 are valid bounded JSON, not raw markup.
 network_count=3;memset(networks[0].ssid,1,32);networks[0].ssid[32]=0;
 strcpy(networks[1].ssid,"<script>\"\\\n\t");strcpy(networks[2].ssid,"İİİİİİİİİİİİİİİİ");
 get_networks(&req);FILE*f=fopen(argv[1],"w");assert(f);fputs(response,f);fclose(f);
 f=fopen(argv[2],"w");assert(f);fputs(page,f);fclose(f);assert(live_allocs==0);
 puts("PASS: RAM16/dedupe/strongest bounds, full32-byte UTF-8/escaping/empty/invalid text; bounded channel scans, timeout/event/alloc/country/driver recovery, cleanup, cache retention, cooldown/single worker, no lock during HTTP and GET never scans");
}
'''
(out/'scan.c').write_text(pre+page+code+tests)
subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-Wno-deprecated-declarations','-fsanitize=address,undefined','-g',str(out/'scan.c'),'-o',str(out/'scan')],check=True)
subprocess.run([str(out/'scan'),str(out/'networks.json'),str(out/'index.html')],check=True)
import json
payload=json.loads((out/'networks.json').read_text());assert len(payload['networks'][2]['ssid'].encode())==32
# Check ownership/setup invariants on the actual app, rather than replacing its radio calls in a mock.
app=s[s.index('static void run(void)'):]
assert app.index('wifi_on();')<app.index('network_scan();')<app.index('esp_wifi_set_mode(WIFI_MODE_APSTA)')<app.index('httpd_start(')
assert '.uri = "/networks/scan"' in app and '.method = HTTP_POST' in app
scan=code[code.index('static void network_scan(void)'):code.index('static esp_err_t network_reply')]
for forbidden in ['esp_wifi_stop(', 'esp_wifi_start(', 'esp_wifi_set_mode(', 'esp_wifi_set_channel(', 'wifi_connect(', 'cfg_set(', 'ui_frame(']: assert forbidden not in scan
js=Path(__file__).with_suffix('.js')
subprocess.run(['node',str(js),str(out/'index.html')],check=True)
print('Evidence and local preview:',out)
