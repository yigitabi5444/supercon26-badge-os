#include <string.h>
#include <strings.h>
#include <time.h>
#include "sdkconfig.h"
#include "esp_netif_sntp.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "badge/badge.h"
#include "package_http.h"
#include "video_asset.h"

enum { SYNC_MS=10000, TIME_RECHECK_S=3600 };
static time_t checked_at;
bool am_https_clock_ready(void)
{
    time_t now=time(NULL);
    // A plausible RTC date is not proof of a recent sync. Reset forgets this RAM stamp.
    return checked_at && now>1700000000 && now>=checked_at && now-checked_at<TIME_RECHECK_S;
}
am_result_t am_https_link_ready(bool (*cancel)(void *),void *owner,am_progress_t progress,uint32_t wifi_ms)
{
    if(!cancel || !wifi_ms || wifi_ms>20000 || !am_https_enabled())return AM_DISABLED;
    if(cancel(owner))return AM_CANCELLED;
    if(!wifi_connected()) {
        if(progress)progress("Connecting to Wi-Fi...");
        bool joined=wifi_connect((int)wifi_ms);
        if(cancel(owner))return AM_CANCELLED;
        if(!joined)return AM_WIFI_FAILED;
    }
    if(am_https_clock_ready())return AM_OK;
    if(progress)progress("Syncing time...");
    esp_sntp_config_t config=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if(esp_netif_sntp_init(&config)!=ESP_OK)return AM_TIME_FAILED;
    int64_t deadline=esp_timer_get_time()+(int64_t)SYNC_MS*1000;
    am_result_t result=AM_TIME_FAILED;
    do {
        if(cancel(owner)){result=AM_CANCELLED;break;}
        if(esp_netif_sntp_sync_wait(pdMS_TO_TICKS(250))==ESP_OK) {
            time_t now=time(NULL);
            if(now>1700000000){checked_at=now;result=AM_OK;}
            break;
        }
    } while(esp_timer_get_time()<deadline);
    esp_netif_sntp_deinit(); // Only our successful init is ours to stop.
    return cancel(owner)?AM_CANCELLED:result;
}
am_result_t am_https_prepare(const am_source_config_t *config,bool (*cancel)(void *),void *owner,
                             am_progress_t progress)
{
    if(!cancel)return AM_IO;
    if(!am_https_enabled() || !am_source_config_valid(config))return AM_DISABLED;
    if(cancel(owner))return AM_CANCELLED;
    if(am_https_clock_ready())return AM_OK; // Offline/recent entry needs no radio.
    am_result_t result=am_https_link_ready(cancel,owner,progress,AM_WIFI_MS);
    wifi_off();ranging_start();
    return result;
}

bool am_https_enabled(void)
{
#if defined(CONFIG_MBEDTLS_HAVE_TIME_DATE) && CONFIG_MBEDTLS_HAVE_TIME_DATE
    return true;
#else
    return false;
#endif
}
static am_result_t live(am_https_t *s)
{
    if(s->cancel(s->owner))return AM_CANCELLED;
    return esp_timer_get_time()<s->deadline?AM_OK:AM_IO;
}
am_result_t am_https_begin(am_https_t *s,const am_source_config_t *config,bool (*cancel)(void *),void *owner,
                           am_progress_t progress)
{
    if(!s || !cancel)return AM_IO;
    memset(s,0,sizeof *s);
    if(!am_https_enabled() || !am_source_config_valid(config))return AM_DISABLED;
    s->cancel=cancel;s->owner=owner;s->config=config;
    if(cancel(owner))return AM_CANCELLED;
    s->radio_owned=true;
    am_result_t result=am_https_link_ready(cancel,owner,progress,AM_WIFI_MS);
    if(result!=AM_OK)return result;
    if(progress)progress("Downloading...");
    s->deadline=esp_timer_get_time()+(int64_t)AM_FETCH_MS*1000;
    return live(s);
}
void am_https_end(am_https_t *s)
{
    if(s && s->radio_owned){wifi_off();ranging_start();s->radio_owned=false;}
}
static esp_err_t header(esp_http_client_event_t *e)
{
    am_https_t *s=e->user_data;
    if(e->event_id!=HTTP_EVENT_ON_HEADER)return ESP_OK;
    if(!strcasecmp(e->header_key,"Location")) {
        size_t n=strnlen(e->header_value,AM_URL_MAX+1);
        if(s->location_seen || !n || n>AM_URL_MAX)s->header_bad=true;
        else memcpy(s->location,e->header_value,n+1);
        s->location_seen=true;
    }
    // Compressed transfer is unnecessary for these small, exactly signed bytes.
    if(!strcasecmp(e->header_key,"Content-Encoding") && strcasecmp(e->header_value,"identity"))s->header_bad=true;
    return ESP_OK;
}
static am_result_t budget(am_https_t *s,esp_http_client_handle_t c)
{
    am_result_t r=live(s);if(r!=AM_OK)return r;
    int64_t left=(s->deadline-esp_timer_get_time()+999)/1000;
    if(left<=0)return AM_IO;
    return esp_http_client_set_timeout_ms(c,(int)(left<AM_HTTP_IO_MS?left:AM_HTTP_IO_MS))==ESP_OK?AM_OK:AM_IO;
}
static am_result_t open_request(am_https_t *s,const char *url,size_t cap,
                                esp_http_client_handle_t *client,size_t *length)
{
    *client=NULL;*length=0;
    if(!s || !s->cancel || !s->radio_owned || !am_source_url_allowed(s->config,url))return AM_DISABLED;
    am_result_t r=live(s);if(r!=AM_OK)return r;
    esp_http_client_handle_t c=esp_http_client_init(&(esp_http_client_config_t){
        .url=url,.cert_pem=s->config->tls_ca_pem,
        .crt_bundle_attach=s->config->local_authority?NULL:esp_crt_bundle_attach,
        .transport_type=HTTP_TRANSPORT_OVER_SSL,
        .timeout_ms=AM_HTTP_IO_MS,.buffer_size=2048,.buffer_size_tx=4096,
        .disable_auto_redirect=true,.event_handler=header,.user_data=s,.user_agent="badge-os-app-manager",
    });
    if(!c)return AM_IO;
    for(unsigned hops=0;;hops++) {
        s->header_bad=s->location_seen=false;s->location[0]=0;
        r=budget(s,c);if(r!=AM_OK)break;
        if(esp_http_client_open(c,0)!=ESP_OK){r=AM_IO;break;}
        r=budget(s,c);if(r!=AM_OK)break;
        int64_t declared=esp_http_client_fetch_headers(c);
        r=live(s);if(r!=AM_OK)break;
        if(declared<0 || s->header_bad){r=AM_IO;break;}
        int status=esp_http_client_get_status_code(c);
        if(status==301 || status==302 || status==303 || status==307 || status==308) {
            if(s->config->local_authority || hops==AM_REDIRECT_MAX || !s->config->redirect_host || !s->location_seen ||
               !am_source_url_valid(s->location,s->config->redirect_host)){r=AM_DISABLED;break;}
            // Validate absolute HTTPS destination BEFORE SDK URL mutation or another open.
            if(esp_http_client_close(c)!=ESP_OK || esp_http_client_set_url(c,s->location)!=ESP_OK){r=AM_IO;break;}
            continue;
        }
        if(status!=200 || declared<=0 || (uint64_t)declared>cap || esp_http_client_is_chunked_response(c)) {
            r=declared>(int64_t)cap?AM_LIMIT:AM_IO;break;
        }
        *client=c;*length=(size_t)declared;return AM_OK;
    }
    if(esp_http_client_cleanup(c)!=ESP_OK)r=AM_IO;
    return r;
}
static am_result_t get_bytes(void *context,const char *url,uint8_t *out,size_t cap,size_t *size,bool doom)
{
    am_https_t *s=context;
    if(size)*size=0;
    if(!out || !size || !cap || cap>(doom?AM_DOOM_PAYLOAD_MAX:AM_PAYLOAD_MAX))return AM_DISABLED;
    esp_http_client_handle_t c=NULL;size_t declared=0;
    am_result_t r=open_request(s,url,cap,&c,&declared);
    if(r!=AM_OK)return r;
    size_t got=0;
    while(got<declared) {
        r=budget(s,c);if(r!=AM_OK)break;
        // Preserve the small-app read budget; Doom alone uses bounded 4096-byte batches.
        size_t amount=doom?declared-got:1;if(amount>4096)amount=4096;
        int n=esp_http_client_read(c,(char *)out+got,(int)amount);
        if(n<=0 || (size_t)n>amount){r=AM_IO;break;}
        got+=(size_t)n;
    }
    if(r==AM_OK)r=live(s);
    if(r==AM_OK && (s->header_bad || !esp_http_client_is_complete_data_received(c)))r=AM_IO;
    if(r==AM_OK)*size=got;
    if(esp_http_client_cleanup(c)!=ESP_OK)r=AM_IO;
    if(r!=AM_OK)*size=0;
    return r;
}
am_result_t am_https_get(void *c,const char *u,uint8_t *p,size_t n,size_t *got)
{ return get_bytes(c,u,p,n,got,false); }
am_result_t am_https_doom_get(void *c,const char *u,uint8_t *p,size_t n,size_t *got)
{ return get_bytes(c,u,p,n,got,true); }
am_result_t am_https_video_open(am_https_t *s,const char *url,am_https_video_t *stream)
{
    if(!stream)return AM_IO;
    memset(stream,0,sizeof *stream);esp_http_client_handle_t c=NULL;size_t length=0;
    am_result_t r=open_request(s,url,AM_VIDEO_CLIP_BYTES,&c,&length);
    if(r!=AM_OK)return r;
    if(length!=AM_VIDEO_CLIP_BYTES) {
        return esp_http_client_cleanup(c)==ESP_OK?AM_LIMIT:AM_IO;
    }
    *stream=(am_https_video_t){s,c,0};return AM_OK;
}
am_result_t am_https_video_read(void *ctx,uint32_t offset,uint8_t *out,size_t cap,size_t *got)
{
    am_https_video_t *stream=ctx;if(got)*got=0;
    if(!stream || !stream->client || !out || !got || !cap || cap>AM_ASSET_CHUNK || offset!=stream->offset)
        return AM_DISABLED;
    am_https_t *s=stream->session;esp_http_client_handle_t c=stream->client;
    if(offset>AM_VIDEO_CLIP_BYTES)return AM_LIMIT;
    am_result_t r=live(s);if(r!=AM_OK)return r;
    size_t n=AM_VIDEO_CLIP_BYTES-offset;if(n>cap)n=cap;
    size_t filled=0;
    while(filled<n) {
        r=budget(s,c);if(r!=AM_OK)return r;
        int read=esp_http_client_read(c,(char *)out+filled,(int)(n-filled));
        if(read<=0 || (size_t)read>n-filled)return AM_IO;
        filled+=(size_t)read;stream->offset+=(uint32_t)read;
    }
    if(stream->offset==AM_VIDEO_CLIP_BYTES &&
       (s->header_bad || !esp_http_client_is_complete_data_received(c)))return AM_IO;
    *got=filled;return live(s);
}
am_result_t am_https_video_close(am_https_video_t *stream)
{
    if(!stream)return AM_IO;
    am_result_t r=AM_OK;
    if(stream->client && esp_http_client_cleanup(stream->client)!=ESP_OK)r=AM_IO;
    memset(stream,0,sizeof *stream);return r;
}
