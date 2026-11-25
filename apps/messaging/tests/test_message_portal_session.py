#!/usr/bin/env python3
"""Actual portal start/context functions: stable sessions and bounded failed setup. No Wi-Fi/device access."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3];source=(root/'apps/messaging/message_portal.c').read_text();out=Path(tempfile.mkdtemp(prefix='badge-msg-session-'))
def fn(name):
 a=source.index(name);a=source.rfind('\n',0,a)+1;b=source.index('{',a);d=1;i=b+1
 while d:d+=(source[i]=='{')-(source[i]=='}');i+=1
 return source[a:i]+'\n'
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "message_store.h"
typedef void *SemaphoreHandle_t;
typedef int esp_err_t;typedef void *httpd_handle_t;typedef struct {int unused;} httpd_req_t;
typedef struct {int stack_size,lru_purge_enable,recv_wait_timeout;} httpd_config_t;
typedef struct {const char *uri;int method;int (*handler)(httpd_req_t*);} httpd_uri_t;
typedef struct {struct {int channel,max_connection,authmode,ssid_len;uint8_t ssid[32],password[64];} ap;} wifi_config_t;
#define ESP_OK 0
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_MODE_AP 2
#define WIFI_IF_AP 1
#define HTTP_GET 1
#define HTTP_POST 2
#define HTTPD_404_NOT_FOUND 404
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define pdPASS 1
#define portMAX_DELAY -1
static int fail_at,setup_ops,mutexes,randoms,wifi_ons,detaches,servers,routes,tasks,depth;
static SemaphoreHandle_t xSemaphoreCreateMutex(void){setup_ops++;mutexes++;return fail_at==1?NULL:(void*)1;}
static void xSemaphoreTake(SemaphoreHandle_t m,int wait){assert(m&&depth++==0);}
static void xSemaphoreGive(SemaphoreHandle_t m){assert(m&&--depth==0);}
static void esp_fill_random(void *out,size_t n){setup_ops++;memset(out,++randoms,n);}
void badge_id(char id[5]){strcpy(id,"1A2B");}
void messaging_transport(message_send_fn send){assert(!send);setup_ops++;detaches++;}
void wifi_on(void){setup_ops++;wifi_ons++;}
static void esp_wifi_stop(void){setup_ops++;}
static void *esp_netif_get_handle_from_ifkey(const char *key){setup_ops++;return NULL;}
static void *esp_netif_create_default_wifi_ap(void){setup_ops++;return fail_at==2?NULL:(void*)2;}
static int esp_wifi_set_mode(int mode){assert(mode==WIFI_MODE_AP);setup_ops++;return fail_at==3?-1:0;}
static int esp_wifi_set_config(int which,const wifi_config_t *c){assert(which==WIFI_IF_AP&&c->ap.channel==1&&c->ap.max_connection==2&&c->ap.authmode==WIFI_AUTH_WPA2_PSK);setup_ops++;return fail_at==4?-1:0;}
static int esp_wifi_start(void){setup_ops++;return fail_at==5?-1:0;}
static int httpd_start(httpd_handle_t *server,const httpd_config_t *c){assert(c->stack_size==6144&&c->lru_purge_enable&&c->recv_wait_timeout==3);setup_ops++;servers++;*server=(void*)3;return fail_at==6?-1:0;}
static int httpd_register_uri_handler(httpd_handle_t s,const httpd_uri_t *r){assert(s&&r->handler);setup_ops++;routes++;return fail_at==7?-1:0;}
static void httpd_register_err_handler(httpd_handle_t s,int code,int (*fn)(httpd_req_t*,int)){setup_ops++;}
static int xTaskCreate(void (*fn)(void*),const char *name,int stack,void *arg,int pri,void *handle){assert(fn&&stack==3072);setup_ops++;tasks++;return fail_at==8?0:pdPASS;}
static int page(httpd_req_t *r){return 0;}static int state(httpd_req_t *r){return 0;}static int action(httpd_req_t *r){return 0;}
static int redirect(httpd_req_t *r,int code){return 0;}static void dns(void *arg){}
'''
a=source.index('static char ssid[');b=source.index('static void select_thread(',a);code+=source[a:b]
for name in ['static void select_thread(', 'void message_portal_selected(', 'bool message_portal_start(', 'const char *message_portal_ssid(', 'const char *message_portal_password(']:code+=fn(name)
code+=r'''
static void reset(void){started=attempted=false;selected_lock=NULL;memset(selected,0,16);setup_ops=mutexes=randoms=wifi_ons=detaches=servers=routes=tasks=depth=0;}
int main(void){
 uint8_t a[16]={1},b[16]={2},got[16],zero[16]={0};reset();
 assert(message_portal_start(a));message_portal_selected(got);assert(!memcmp(got,a,16));
 char old_ssid[20],old_password[17],old_key[33];strcpy(old_ssid,message_portal_ssid());strcpy(old_password,message_portal_password());strcpy(old_key,key);
 int ops=setup_ops;
 for(int i=0;i<20;i++){
  assert(message_portal_start(b));message_portal_selected(got);assert(!memcmp(got,b,16));
  assert(message_portal_start(NULL));message_portal_selected(got);assert(!memcmp(got,zero,16));
  select_thread(a);message_portal_selected(got);assert(!memcmp(got,a,16)); // existing HTTP send/open context
  assert(setup_ops==ops&&!strcmp(old_ssid,ssid)&&!strcmp(old_password,password)&&!strcmp(old_key,key));
 }
 assert(mutexes==1&&randoms==2&&wifi_ons==1&&detaches==1&&servers==1&&routes==3&&tasks==1&&!depth);
 for(fail_at=1;fail_at<=8;fail_at++){
  reset();assert(!message_portal_start(a));assert(attempted&&!started);ops=setup_ops;
  for(int i=0;i<20;i++)assert(!message_portal_start(b)&&setup_ops==ops);
  assert(mutexes==1&&servers<=1&&tasks<=1&&!depth); // retry requires Home; no duplicate setup/credential churn
 }
 puts("PASS actual portal: mutex context changes, 40 reopens retain AP/key/credentials, each setup-failure stage refuses duplicate allocation/retry until Home");
}
'''
p=out/'session.c';p.write_text(code)
subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-g','-I',str(root/'components/os/messages'),'-I',str(root/'components/os/include'),'-I',str(root/'apps/messaging'),str(p),str(root/'components/os/messages/message_store.c'),'-o',str(out/'session')],check=True)
subprocess.run([str(out/'session')],check=True)
print('Portal session harness:',out)
