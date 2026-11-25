// No socket or network. Resolver/HTTP/TLS/SNTP/heap calls are counted and constrained.
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_netif_sntp.h"
#include "badge/badge.h"
#include "native_host_double.h"
#include "had_sdk_double.h"
had_sdk_fixture_t had_sdk;
struct local_http_double {
    const esp_http_client_config_t *unused;
    esp_err_t (*event)(esp_http_client_event_t *);void *user;
    char host[254],path[400];bool tls;
    const uint8_t *body;size_t length;
};
static struct local_http_double client;
static bool client_live;
static char resolved_host[254];
static time_t epoch=2000000000;
void had_sdk_reset(void)
{
    assert(!client_live && atomic_load(&had_sdk.allocations)==atomic_load(&had_sdk.frees));
    memset(&had_sdk,0,sizeof had_sdk);had_sdk.status=200;had_sdk.declared=-2;epoch+=4000;
    atomic_init(&had_sdk.blocked,false);atomic_init(&had_sdk.reading,false);
    atomic_init(&had_sdk.allocations,0);atomic_init(&had_sdk.frees,0);
}
void *heap_caps_malloc(size_t n,unsigned caps)
{(void)caps;void *p=malloc(n);if(p){nh_memory(p,n,NH_PSRAM,true);atomic_fetch_add(&had_sdk.allocations,1);}return p;}
void *heap_caps_calloc(size_t n,size_t size,unsigned caps)
{if(size && n>SIZE_MAX/size)return NULL;void *p=heap_caps_malloc(n*size,caps);if(p)memset(p,0,n*size);return p;}
void heap_caps_free(void *p)
{if(p){nh_forget(p);atomic_fetch_add(&had_sdk.frees,1);free(p);}}
void *heap_caps_realloc(void *p,size_t n,unsigned caps)
{
    if(!p)return heap_caps_malloc(n,caps);
    nh_forget(p);void *next=realloc(p,n);assert(next);nh_memory(next,n,NH_PSRAM,true);return next;
}
bool wifi_connect(int timeout)
{assert(timeout==20000);had_sdk.joins++;had_sdk.radio=true;atomic_fetch_add(&nh_time_ms,10);return !had_sdk.join_fail;}
bool wifi_connected(void){return had_sdk.radio && !had_sdk.join_fail;}
void wifi_off(void){assert(!client_live && had_sdk.radio);had_sdk.radio=false;had_sdk.radio_off++;}
void ranging_start(void){assert(!had_sdk.radio && !client_live);had_sdk.ranging++;}
time_t am_test_time(time_t *out){if(out)*out=epoch;return epoch;}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c)
{assert(!strcmp(c->server,"pool.ntp.org"));had_sdk.syncs++;return had_sdk.time_fail?-1:ESP_OK;}
esp_err_t esp_netif_sntp_sync_wait(TickType_t timeout){assert(timeout==250);return ESP_OK;}
void esp_netif_sntp_deinit(void){}
esp_err_t esp_crt_bundle_attach(void *ctx){(void)ctx;return ESP_OK;}
int getaddrinfo(const char *node,const char *service,const struct addrinfo *hints,struct addrinfo **out)
{
    assert(node && !service && hints->ai_family==AF_INET && hints->ai_socktype==SOCK_STREAM);had_sdk.resolves++;
    snprintf(resolved_host,sizeof resolved_host,"%s",node);
    struct addrinfo *a=calloc(1,sizeof *a);struct sockaddr_in *address=calloc(1,sizeof *address);assert(a && address);
    address->sin_family=AF_INET;
    if(inet_pton(AF_INET,node,&address->sin_addr)!=1)
        assert(inet_pton(AF_INET,had_sdk.private_dns?"127.0.0.1":"8.8.8.8",&address->sin_addr)==1);
    a->ai_family=AF_INET;a->ai_addrlen=sizeof *address;a->ai_addr=(void *)address;*out=a;return 0;
}
void freeaddrinfo(struct addrinfo *a){assert(a && !a->ai_next);free(a->ai_addr);free(a);}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c)
{
    assert(!client_live && !c->url && !strcmp(c->host,"8.8.8.8") && c->path[0]=='/');
    assert(c->disable_auto_redirect && !c->skip_cert_common_name_check && c->buffer_size==16384 && c->timeout_ms==15000);
    assert(!strcmp(c->user_agent,"badge-os") && !c->cert_pem);
    bool tls=c->transport_type==HTTP_TRANSPORT_OVER_SSL;
    assert(tls?(c->port==443 && c->crt_bundle_attach==esp_crt_bundle_attach && c->common_name):
        (c->port==80 && !c->crt_bundle_attach && !c->common_name));
    if(had_sdk.init_fail)return NULL;
    memset(&client,0,sizeof client);client.event=c->event_handler;client.user=c->user_data;client.tls=tls;
    snprintf(client.host,sizeof client.host,"%s",resolved_host);
    if(tls)assert(!strcmp(client.host,c->common_name));
    snprintf(client.path,sizeof client.path,"%s",c->path);
    bool image=had_sdk.image && strstr(c->path,"image.png");
    client.body=image?had_sdk.image:had_sdk.body;client.length=image?had_sdk.image_length:had_sdk.length;
    client_live=true;had_sdk.offset=0;return &client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c,const char *key,const char *value)
{
    assert(c==&client && client_live);
    if(!strcmp(key,"Host")) {
        assert(!strcmp(value,client.host));
    } else assert(!strcmp(key,"Accept-Encoding") && (!strcmp(value,"gzip") || !strcmp(value,"identity")));
    return ESP_OK;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t c,int timeout)
{assert(c==&client && timeout>0 && timeout<=15000);return ESP_OK;}
esp_err_t esp_http_client_open(esp_http_client_handle_t c,int size)
{assert(c==&client && !size && had_sdk.radio);had_sdk.opens++;had_sdk.connects++;return ESP_OK;}
static void event_header(const char *name,const char *value)
{
    esp_http_client_event_t e={.event_id=HTTP_EVENT_ON_HEADER,.client=&client,.user_data=client.user,
        .header_key=(char *)name,.header_value=(char *)value};assert(client.event(&e)==ESP_OK);
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c)
{
    assert(c==&client);
    if(had_sdk.encoding)event_header("Content-Encoding",had_sdk.encoding);
    if(had_sdk.location)event_header("Location",had_sdk.location);
    return had_sdk.declared==-2?(int64_t)client.length:had_sdk.declared;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c){assert(c==&client);return had_sdk.status;}
bool esp_http_client_is_chunked_response(esp_http_client_handle_t c){assert(c==&client);return !had_sdk.declared;}
int esp_http_client_read(esp_http_client_handle_t c,char *out,int cap)
{
    assert(c==&client && cap>0);had_sdk.reads++;atomic_store(&had_sdk.reading,true);
    while(atomic_load(&had_sdk.blocked))nh_sleep();
    size_t n=client.length-had_sdk.offset;if(n>(size_t)cap)n=(size_t)cap;
    if(had_sdk.chunk && n>had_sdk.chunk)n=had_sdk.chunk;
    memcpy(out,client.body+had_sdk.offset,n);had_sdk.offset+=n;return (int)n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c)
{assert(c==&client);return !had_sdk.incomplete && had_sdk.offset==client.length;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c)
{assert(c==&client && client_live);client_live=false;had_sdk.cleans++;return ESP_OK;}
esp_err_t esp_http_client_close(esp_http_client_handle_t c){assert(c==&client);return ESP_OK;}
esp_err_t esp_http_client_set_url(esp_http_client_handle_t c,const char *url)
{(void)c;(void)url;assert(0);return ESP_FAIL;}
