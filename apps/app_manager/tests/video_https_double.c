// File-backed SDK HTTP/TLS/radio double. Never opens a socket or listener.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_http_client.h"
#include "esp_netif_sntp.h"
#include "esp_crt_bundle.h"
#include "video_https_double.h"
#include "native_host_double.h"

video_https_fixture_t vh;
struct local_http_double { esp_http_client_config_t config;FILE *file;size_t size,got;bool clip; };
time_t am_test_time(time_t *out){time_t now=1700001000;if(out)*out=now;return now;}
bool wifi_connect(int ms){assert(ms==AM_WIFI_MS);vh.joins++;vh.radio=true;return true;}
bool wifi_connected(void){return vh.radio;}
void wifi_off(void){assert(!vh.clients);vh.radio=false;vh.offs++;}
void ranging_start(void){assert(!vh.radio && !vh.clients);}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c)
{assert(vh.radio && !strcmp(c->server,"pool.ntp.org"));vh.syncs++;return ESP_OK;}
esp_err_t esp_netif_sntp_sync_wait(TickType_t ticks){assert(ticks==250);return ESP_OK;}
void esp_netif_sntp_deinit(void){}
esp_err_t esp_crt_bundle_attach(void *c){(void)c;assert(0);return ESP_FAIL;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c)
{
    assert(vh.radio && !vh.clients && c->transport_type==HTTP_TRANSPORT_OVER_SSL && c->disable_auto_redirect);
    assert(!c->skip_cert_common_name_check && !c->common_name && !c->use_global_ca_store);
    assert(c->cert_pem==vh.config->tls_ca_pem && !c->crt_bundle_attach);
    assert(c->buffer_size==2048 && c->buffer_size_tx==4096 && c->timeout_ms<=AM_HTTP_IO_MS);
    assert(am_source_url_allowed(vh.config,c->url));
    struct local_http_double *h=calloc(1,sizeof *h);assert(h);h->config=*c;vh.clients++;return h;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t c,int ms)
{assert(c && ms>0 && ms<=AM_HTTP_IO_MS);return ESP_OK;}
esp_err_t esp_http_client_open(esp_http_client_handle_t c,int len)
{
    assert(!len && !c->file);const char *name=strrchr(c->config.url,'/');assert(name);name++;
    c->clip=!strcmp(name,"clip.vid");bool asset=!strncmp(name,"clip.",5);
    assert(!vh.locked || c->clip);
    if(c->clip){assert(vh.locked && vh.deferred);vh.clip_opens++;}
    char path[512];int n=snprintf(path,sizeof path,"%s/%s",asset?vh.assets:vh.code,name);
    assert(n>0 && (size_t)n<sizeof path);c->file=fopen(path,"rb");assert(c->file);
    assert(!fseek(c->file,0,SEEK_END));c->size=(size_t)ftell(c->file);rewind(c->file);vh.opens++;return ESP_OK;
}
static void header(esp_http_client_handle_t c,char *name,char *value)
{
    esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_HEADER,.client=c,.user_data=c->config.user_data,
        .header_key=name,.header_value=value};assert(c->config.event_handler(&event)==ESP_OK);
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c)
{
    if(c->clip && !strcmp(vh.mode,"gzip"))header(c,"Content-Encoding","gzip");
    if(c->clip && !strcmp(vh.mode,"redirect"))header(c,"Location","https://other.example/clip.vid");
    return (int64_t)c->size-(c->clip && !strcmp(vh.mode,"length"));
}
int esp_http_client_get_status_code(esp_http_client_handle_t c)
{return c->clip && !strcmp(vh.mode,"missing")?404:c->clip && !strcmp(vh.mode,"redirect")?302:200;}
bool esp_http_client_is_chunked_response(esp_http_client_handle_t c)
{return c->clip && !strcmp(vh.mode,"chunked");}
int esp_http_client_read(esp_http_client_handle_t c,char *out,int cap)
{
    assert(cap>0 && cap<=4096);if((unsigned)cap>vh.max_read)vh.max_read=(unsigned)cap;
    if(c->clip)vh.clip_reads++;
    size_t left=c->size-c->got;
    if(c->clip && !strcmp(vh.mode,"short")){if(left)left--;}
    size_t n=(size_t)cap;if(n>left)n=left;
    size_t got=fread(out,1,n,c->file);assert(got==n);
    if(c->clip && !strcmp(vh.mode,"tamper") && c->got<=4000 && got>4000-c->got)out[4000-c->got]^=1;
    if(!strcmp(strrchr(c->config.url,'/'),"/clip.receipt") && !strcmp(vh.mode,"bad-receipt") && c->got==0 && got)out[0]^=1;
    c->got+=got;atomic_fetch_add(&nh_time_ms,1);
    if(c->clip && !strcmp(vh.mode,"cancel") && c->got>=8192)vh.cancelled=true;
    if(c->clip && !strcmp(vh.mode,"timeout"))atomic_fetch_add(&nh_time_ms,21000);
    return (int)got;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c){return c->got==c->size;}
esp_err_t esp_http_client_close(esp_http_client_handle_t c){assert(c);return ESP_OK;}
esp_err_t esp_http_client_set_url(esp_http_client_handle_t c,const char *url)
{(void)c;(void)url;assert(!"Local redirect must be rejected before SDK URL mutation");return ESP_FAIL;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c)
{assert(c && vh.clients==1);if(c->file)fclose(c->file);free(c);vh.clients--;vh.cleans++;return ESP_OK;}
