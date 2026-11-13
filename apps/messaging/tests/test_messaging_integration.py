#!/usr/bin/env python3
"""Run actual ranging/transport/receive functions with host mocks; boot/storage is checked by the integration owner. No ESP32/flash access."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3];s=(root/'components/os/ranging.c').read_text(); wifi=(root/'components/os/wifi.c').read_text();out=Path(tempfile.mkdtemp(prefix='badge-msg-hooks-'))
def function(name,source=None):
 global s
 original=s
 if source is not None:s=source
 a=s.index(name);a=s.rfind('\n',0,a)+1;b=s.index('{',a);depth=1;i=b+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 result=s[a:i]+'\n';s=original;return result
code=r'''
#include <assert.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "badge/badge.h"
#include "messaging.h"
typedef int esp_err_t;typedef int SemaphoreHandle_t;typedef int EventGroupHandle_t;
typedef int esp_event_base_t;
typedef struct {int status;unsigned dist_est,rtt_est;uint8_t peer_mac[6];} wifi_event_ftm_report_t;
#define FTM_STATUS_SUCCESS 0
#define WIFI_EVENT_FTM_REPORT 3
static void xEventGroupSetBits(int a,int b){}
typedef struct {const uint8_t *src_addr;} esp_now_recv_info_t;
typedef struct {int nvs_enable;} wifi_init_config_t;
typedef struct {struct {char ssid[32];int ssid_len,channel,authmode,ssid_hidden,max_connection,beacon_interval;bool ftm_responder;} ap;} wifi_config_t;
typedef struct {uint8_t peer_addr[6];int channel,ifidx;} esp_now_peer_info_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){1})
#define ESP_OK 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ESP_LOG_ERROR 1
#define ESP_LOG_WARN 2
static void esp_log_level_set(const char *tag,int level) {}
#define ESP_ERR_ESPNOW_EXIST 2
#define WIFI_MODE_STA 1
#define WIFI_MODE_APSTA 2
#define WIFI_AUTH_OPEN 0
#define WIFI_IF_AP 1
#define WIFI_IF_STA 0
#define WIFI_STORAGE_RAM 0
#define WIFI_PS_NONE 0
#define WIFI_SECOND_CHAN_NONE 0
#define ESP_MAC_WIFI_STA 0
#define WIFI_EVENT 1
#define IP_EVENT 2
#define ESP_EVENT_ANY_ID 0
#define IP_EVENT_STA_GOT_IP 1
#define IP_EVENT_STA_LOST_IP 2
#define portMAX_DELAY -1
#define pdMS_TO_TICKS(x) (x)
#define ESP_ERROR_CHECK(x) assert((x)==0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
static EventGroupHandle_t wifi_ev;static bool wifi_join,cfg_store_ok;
static int init_error,deinits,wifi_nvs=-1,registers,send_result,sends,receives,locks,stop_calls,peer_error,receive_error;
static message_send_fn attached;
static void (*owner)(const esp_now_recv_info_t*,const uint8_t*,int);
static int nvs_flash_init(void){return init_error;}static void nvs_flash_deinit(void){deinits++;}
static int xEventGroupCreate(void){return 1;}static void xEventGroupClearBits(int a,int b){}
static int xSemaphoreCreateMutex(void){return 1;}static int xSemaphoreCreateBinary(void){return 2;}
static void xSemaphoreTake(int a,int b){locks++;}static void xSemaphoreGive(int a){}
static int esp_netif_init(void){return 0;}static int esp_event_loop_create_default(void){return 0;}
static void esp_netif_create_default_wifi_sta(void){}
static void esp_event_handler_register(int a,int b,void *c,void *d){}
static void *on_wifi;
static int esp_wifi_init(const wifi_init_config_t *c){wifi_nvs=c->nvs_enable;return 0;}
static int esp_wifi_set_storage(int a){return 0;}static int esp_wifi_set_mode(int a){return 0;}
static int esp_wifi_start(void){return 0;}static int esp_wifi_stop(void){stop_calls++;return 0;}
static void esp_wifi_disconnect(void){}static int esp_wifi_set_ps(int a){return 0;}
static int esp_wifi_set_config(int a,const wifi_config_t *c){assert(c->ap.channel==1);return 0;}
static int esp_wifi_set_channel(int a,int b){assert(a==1);return 0;}
static void esp_read_mac(uint8_t *m,int a){memset(m,1,6);}
static int esp_now_init(void){return 0;}
static int esp_now_register_recv_cb(void (*fn)(const esp_now_recv_info_t*,const uint8_t*,int)){registers++;if(!receive_error)owner=fn;return receive_error;}
static int esp_now_add_peer(const esp_now_peer_info_t *p){assert(p->channel==1&&p->peer_addr[0]==255);return peer_error;}
static void esp_now_deinit(void){assert(!attached);owner=NULL;}
static int esp_now_send(const uint8_t *peer,const uint8_t *data,size_t n){assert(peer[0]==255&&n<=MSG_PACKET_MAX);sends++;return send_result;}
static void xTaskCreate(void (*fn)(void*),const char *n,int stack,void *ctx,int pri,void *out){}
static int64_t esp_timer_get_time(void){return 1000000;}
static void rng_task(void *unused){}
void messaging_transport(message_send_fn send){attached=send;}
typedef int QueueHandle_t;static QueueHandle_t packets=1;
typedef struct {uint8_t peer[6],data[MSG_PACKET_MAX];uint16_t size;} packet_t;
static void xQueueSend(int q,const packet_t *p,int ticks){assert(q==1&&ticks==0&&p->size<=MSG_PACKET_MAX);receives++;}
'''
# Actual code, not replicas of the integration decisions.
code+=function('bool messaging_receive(', (root/'components/os/messages/messaging.c').read_text())
code+='bool cfg_storage_ready(void) {return cfg_store_ok;}\n'
code+=function('void wifi_on(',wifi)+function('void wifi_off(',wifi)
code+=s[s.index('#define RNG_CH'):s.index('static void rng_task(void *arg)')]
code+=function('void ranging_start(')+function('void ranging_stop(')
code+=r'''
int main(void){
 cfg_store_ok=true;
 for(int i=0;i<5;i++){
  ranging_start();assert(attached&&owner==rng_recv);int n=registers;ranging_start();assert(registers==n);
  uint8_t peer[6]={2},p[MSG_PACKET_MAX]={0};esp_now_recv_info_t info={peer};
  memcpy(p,"BMSG",4);int old_locks=locks;owner(&info,p,sizeof p);assert(receives&&locks==old_locks);
  rng_beacon_t legacy={.magic={'B','O','S','R'},.n=0};memcpy(p,&legacy,offsetof(rng_beacon_t,r));memcpy(p+offsetof(rng_beacon_t,r),"Legacy",7);
  owner(&info,p,offsetof(rng_beacon_t,r)+12);assert(!strcmp(rng[0].name,"Legacy"));
  // New BOSF results coexist with queue-only BMSG and byte-compatible BOSR.
  rng_fast_t fast={.magic={'B','O','S','F'},.n=2};
  memcpy(fast.r[0].mac,rng_me,6);fast.r[0].cm=1234;fast.r[1].mac[0]=3;fast.r[1].cm=345;
  int msg_before=receives;owner(&info,(const uint8_t*)&fast,5+2*sizeof(rng_pair_t));
  int own=rng_find(rng_me,peer), shared=rng_find(peer,fast.r[1].mac);
  assert(own>=0 && shared>=0 && rng[own].cm==1234 && rng[own].fast && rng[shared].cm==345 && receives==msg_before);
  int old=locks;fast.n=RNG_FAST+1;owner(&info,(const uint8_t*)&fast,sizeof fast);assert(locks==old);
  fast.n=1;owner(&info,(const uint8_t*)&fast,5);assert(locks==old);
  owner(NULL,p,22);owner(&info,NULL,22);owner(&info,p,-1);assert(locks==old);
  int before=sends;send_result=5;assert(!attached(peer,p,22));send_result=0;assert(attached(peer,p,22)&&sends==before+2);
  // wifi_on is the actual shared path used by phone AP, Pong and network apps; Home restores ranging.
  wifi_on();assert(!attached&&!rng_run&&!owner);before=sends;assert(!message_radio_send(peer,p,22)&&sends==before);
  wifi_off();ranging_start();assert(attached&&rng_run);ranging_stop();assert(!attached&&!owner);
 }
 peer_error=5;ranging_start();assert(!attached);ranging_stop();peer_error=0;
 receive_error=5;ranging_start();assert(!attached);ranging_stop();
 puts("PASS actual core: one callback, BOSR legacy beacon/name, BOSF result/bounds, BMSG queue-only path, real send failures, detach before deinit, network takeover/restore, existing ranging restored after takeover");
}
'''
p=out/'hooks.c';p.write_text(code)
subprocess.run(['cc','-std=gnu11','-fsanitize=address,undefined','-Wno-unused-value','-I',str(root/'components/os/messages'),'-I',str(root/'components/os/include'),str(p),str(root/'components/os/messages/message_store.c'),str(root/'components/os/messages/message_sync.c'),'-o',str(out/'hooks')],check=True)
subprocess.run([str(out/'hooks')],check=True)
print('Host radio integration harness:',out)
