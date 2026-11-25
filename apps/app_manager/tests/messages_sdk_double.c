// Resident service uses the original bounded store/codec. Only NVS and SDK I/O are doubled.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "badge/badge.h"
#include "message_store.h"
#include "native_host_double.h"
#include "native_messages_portal.h"
#include "messages_sdk_double.h"
messages_sdk_t messages_sdk;
static message_db_t database;
static const char *store_path;
static httpd_uri_t routes[3];
static char page_key[33];
bool messages_test_handler(esp_err_t (*)(httpd_req_t *));
bool messages_test_dns(void (*)(void *));
int nh_core_btn_get(int);
static int persist(void *ctx,const message_state_t *s,uint64_t generation)
{
    (void)ctx;uint8_t bytes[MSG_SNAPSHOT_BYTES];size_t n=message_pack(s,generation,bytes);assert(n);
    FILE *f=fopen(store_path,"wb");assert(f && fwrite(bytes,1,n,f)==n && !fclose(f));messages_sdk.writes++;return MSG_OK;
}
void messages_sdk_init(const char *path,const char *mode)
{
    store_path=path;messages_sdk=(messages_sdk_t){.available=strcmp(mode,"unavailable")!=0,.fail_server=!strcmp(mode,"failure")};
    database=(message_db_t){.ready=true,.persist=persist};FILE *f=fopen(path,"rb");
    if(f){uint8_t bytes[MSG_SNAPSHOT_BYTES];size_t n=fread(bytes,1,sizeof bytes,f);assert(!ferror(f) && !fclose(f));
        assert(message_unpack(&database.state,&database.generation,bytes,n));}
    else {message_t m={.id={1},.thread={2},.sender={3},.logical=1,.seconds=1794157500,.clock=1};
        strcpy(m.title,"Topic");strcpy(m.body,"Synthetic public message.");strcpy(m.name,"Fixture");assert(message_put(&database,&m,false)==MSG_OK);}
    messages_sdk.writes=0;
}
bool messaging_start(void){messages_sdk.starts++;return messages_sdk.available;}
bool messaging_snapshot(message_state_t *out,uint64_t *revision)
{if(!messages_sdk.available)return false;*out=database.state;if(revision)*revision=database.generation;return true;}
int messaging_read(const uint8_t id[16]){messages_sdk.reads++;return message_mark_read(&database,id);}
const char *messaging_status(void){return messages_sdk.available?"Ready":"Storage unavailable; saved messages kept.";}
void messaging_transport(message_send_fn send){assert(!send);messages_sdk.detaches++;}
int messaging_submit(const uint8_t *a,const uint8_t *b,const char *c,const char *d,uint64_t e)
{(void)a;(void)b;(void)c;(void)d;(void)e;assert(!"POST mutation proof is reused from the unchanged owner portal/store tests");return MSG_IO;}
int messaging_forget(const uint8_t *id){return message_forget(&database,id);}
int messaging_relay(bool on){return message_relay(&database,on);}
void ranging_me(uint8_t out[6]){memset(out,3,6);}
void badge_id(char out[5]){memcpy(out,"abcd",5);}
void wifi_on(void){messages_sdk.wifi++;}
void esp_fill_random(void *out,size_t n){assert(n==16);messages_sdk.randoms++;memset(out,(int)messages_sdk.randoms,n);}
time_t messages_test_time(time_t *out){time_t now=1794200000;if(out)*out=now;return now;}
void *esp_netif_get_handle_from_ifkey(const char *key){assert(!strcmp(key,"WIFI_AP_DEF"));return NULL;}
void *esp_netif_create_default_wifi_ap(void){return (void *)(uintptr_t)1;}
esp_err_t esp_wifi_stop(void){return ESP_OK;}
esp_err_t esp_wifi_set_mode(int mode){assert(mode==WIFI_MODE_AP);return ESP_OK;}
esp_err_t esp_wifi_set_config(int which,const wifi_config_t *config)
{assert(which==WIFI_IF_AP && config->ap.channel==1 && config->ap.max_connection==2 && config->ap.authmode==WIFI_AUTH_WPA2_PSK);
 assert(config->ap.ssid_len==8 && !strcmp((const char *)config->ap.ssid,"Msg-abcd") && strlen((const char *)config->ap.password)==16);return ESP_OK;}
esp_err_t esp_wifi_start(void){return ESP_OK;}
esp_err_t httpd_start(httpd_handle_t *out,const httpd_config_t *config)
{assert(config->stack_size==6144 && config->lru_purge_enable && config->recv_wait_timeout==3);messages_sdk.servers++;
 if(messages_sdk.fail_server)return ESP_FAIL;*out=&messages_sdk;return ESP_OK;}
esp_err_t httpd_register_uri_handler(httpd_handle_t server,const httpd_uri_t *route)
{assert(server==&messages_sdk && messages_sdk.routes<3 && !route->user_ctx && messages_test_handler(route->handler));routes[messages_sdk.routes++]=*route;return ESP_OK;}
esp_err_t httpd_register_err_handler(httpd_handle_t server,int code,esp_err_t (*handler)(httpd_req_t *,httpd_err_code_t))
{assert(server==&messages_sdk && code==HTTPD_404_NOT_FOUND && handler);return ESP_OK;}
static const char *request_header(httpd_req_t *r,const char *name)
{return !strcmp(name,"Host")?r->host:!strcmp(name,"X-Msg-Key")?r->token:NULL;}
size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *name)
{const char *s=request_header(r,name);return s?strlen(s):0;}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *name,char *out,size_t cap)
{const char *s=request_header(r,name);if(!s || strlen(s)>=cap)return ESP_FAIL;strcpy(out,s);return ESP_OK;}
int httpd_req_recv(httpd_req_t *r,char *out,size_t n)
{assert(r->position+n<=r->content_len);memcpy(out,r->body+r->position,n);r->position+=n;return (int)n;}
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *status){r->status=atoi(status);return ESP_OK;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *type){(void)r;(void)type;return ESP_OK;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *value){(void)r;(void)key;(void)value;return ESP_OK;}
esp_err_t httpd_resp_send_chunk(httpd_req_t *r,const char *p,int n)
{
    assert(n>=0 && r->length+(size_t)n<sizeof r->response);
    if(p && n){memcpy(r->response+r->length,p,(size_t)n);r->length+=(size_t)n;r->response[r->length]=0;
        if(n==32){memcpy(page_key,p,32);page_key[32]=0;}}
    return ESP_OK;
}
esp_err_t httpd_resp_send(httpd_req_t *r,const char *p,int n){return httpd_resp_send_chunk(r,p,n);}
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *p){return httpd_resp_send(r,p,(int)strlen(p));}
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *r,const char *p){return httpd_resp_send_chunk(r,p,(int)strlen(p));}
esp_err_t httpd_resp_send_err(httpd_req_t *r,int code,const char *p){r->status=code;return httpd_resp_sendstr(r,p);}
void messages_sdk_phone_check(void)
{
    assert(am_msg_portal_attempted() && messages_sdk.randoms==2 && messages_sdk.servers==1 && messages_sdk.detaches==1 && messages_sdk.wifi==1);
    if(messages_sdk.fail_server){assert(!messages_sdk.routes && !nh_resident_tasks);return;}
    assert(messages_sdk.routes==3 && nh_resident_tasks==1 && messages_test_dns(nh_resident_entry));
    httpd_req_t *r=calloc(1,sizeof *r);assert(r);r->host="192.168.4.1";r->status=200;
    assert(routes[0].method==HTTP_GET && !strcmp(routes[0].uri,"/") && routes[0].handler(r)==ESP_OK);
    assert(strlen(page_key)==32 && !strstr(r->response,"@KEY@"));
    memset(r,0,sizeof *r);r->token=page_key;r->status=200;
    assert(!strcmp(routes[1].uri,"/api/state") && routes[1].handler(r)==ESP_OK && r->status==200);
    assert(strstr(r->response,"Synthetic public message.") && strstr(r->response,"\"read\":true") &&
        strstr(r->response,"\"selected\":\"02000000000000000000000000000000\""));free(r);
}
int btn_get(int timeout)
{
    int result=nh_core_btn_get(timeout);if(timeout!=1000)return result;
    unsigned step=++messages_sdk.reader_keys;
    if(!messages_sdk.available)return step==1?BTN_PLUS:BTN_B;
    if(step==1 || step==2 || step==4)return BTN_MINUS;
    if(step==3)return BTN_B;
    assert(step==5);messages_test_home();assert(0);return -1;
}
