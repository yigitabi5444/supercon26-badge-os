// Worker-only HTTP. Resolve/filter once and pin the address before any connect;
// preserve original Host and TLS SNI/name verification, including every redirect.
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "native_had_job.h"
#include "native_had_url.h"

typedef struct { char location[400],encoding[16];bool bad; } headers_t;
static esp_err_t header(esp_http_client_event_t *event)
{
    if(event->event_id!=HTTP_EVENT_ON_HEADER)return ESP_OK;
    headers_t *h=event->user_data;char *out=NULL;size_t cap=0;
    if(!strcasecmp(event->header_key,"Location")){out=h->location;cap=sizeof h->location;}
    if(!strcasecmp(event->header_key,"Content-Encoding")){out=h->encoding;cap=sizeof h->encoding;}
    if(out) {
        size_t n=strnlen(event->header_value,cap);
        if(n==cap || out[0])h->bad=true;
        else memcpy(out,event->header_value,n+1);
    }
    return ESP_OK;
}
static bool resolve(am_had_job_t *job,const char *host,char address[16])
{
    struct addrinfo hints={.ai_family=AF_INET,.ai_socktype=SOCK_STREAM},*list=NULL;
    if(atomic_load(&job->cancel) || getaddrinfo(host,NULL,&hints,&list))return false;
    bool good=false;
    for(struct addrinfo *p=list;p;p=p->ai_next)if(p->ai_family==AF_INET && p->ai_addrlen>=sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *a=(const void *)p->ai_addr;
        if(am_had_public_ipv4(ntohl(a->sin_addr.s_addr)) &&
           inet_ntop(AF_INET,&a->sin_addr,address,16)){good=true;break;}
    }
    freeaddrinfo(list);return good && !atomic_load(&job->cancel);
}
static bool budget(am_had_job_t *job,esp_http_client_handle_t client,int64_t deadline,bool reading)
{
    int64_t left=deadline-esp_timer_get_time();
    if(atomic_load(&job->cancel) || left<=0)return false;
    int64_t ms=(left+999)/1000;if(reading && ms>250)ms=250;
    return esp_http_client_set_timeout_ms(client,(int)ms)==ESP_OK;
}
bool am_had_fetch_job(am_had_job_t *job)
{
    char current[400];memcpy(current,job->url,sizeof current);current[399]=0;
    uint8_t *raw=heap_caps_malloc(BADGE_HAD_TRANSFER_MAX,MALLOC_CAP_SPIRAM);
    if(!raw){snprintf(job->error,sizeof job->error,"Not enough download memory.");return false;}
    int64_t deadline=esp_timer_get_time()+15000000LL;uint32_t count=0;bool ok=false;
    for(unsigned redirects=0;redirects<=10;redirects++) {
        char host[254],address[16];const char *path;bool tls;
        if(!am_had_url(current,job->kind==1,host,&path,&tls) || !resolve(job,host,address)) {
            snprintf(job->error,sizeof job->error,"Media destination is unavailable.");break;
        }
        headers_t headers={0};
        esp_http_client_handle_t client=esp_http_client_init(&(esp_http_client_config_t){
            .host=address,.path=path,.port=tls?443:80,.transport_type=tls?HTTP_TRANSPORT_OVER_SSL:HTTP_TRANSPORT_OVER_TCP,
            .crt_bundle_attach=tls?esp_crt_bundle_attach:NULL,.common_name=tls?host:NULL,
            .timeout_ms=15000,.buffer_size=16384,.user_agent="badge-os",.disable_auto_redirect=true,
            .event_handler=header,.user_data=&headers});
        if(!client){snprintf(job->error,sizeof job->error,"HTTP initialization failed.");break;}
        bool again=false;char next[400]={0};
        if(esp_http_client_set_header(client,"Host",host)!=ESP_OK ||
           esp_http_client_set_header(client,"Accept-Encoding",job->kind==1?"gzip":"identity")!=ESP_OK ||
           !budget(job,client,deadline,false) || esp_http_client_open(client,0)!=ESP_OK ||
           !budget(job,client,deadline,false))goto close;
        int64_t declared=esp_http_client_fetch_headers(client);
        if(declared<0 || headers.bad || !budget(job,client,deadline,false))goto close;
        int status=esp_http_client_get_status_code(client);
        if(status==301 || status==302 || status==303 || status==307 || status==308) {
            if(!headers.location[0] || redirects==10)goto close;
            if(!am_had_redirect(current,headers.location,job->kind==1,next))goto close;
            again=true;goto close;
        }
        if(status!=200){snprintf(job->error,sizeof job->error,"HTTP %d",status);goto close;}
        if(declared>=BADGE_HAD_TRANSFER_MAX){snprintf(job->error,sizeof job->error,"Download exceeds768 KiB.");goto close;}
        if(headers.encoding[0] && strcasecmp(headers.encoding,"identity") &&
           !(job->kind==1 && !strcasecmp(headers.encoding,"gzip")))goto close;
        count=0;
        for(;;) {
            if(!budget(job,client,deadline,true))goto close;
            uint32_t space=BADGE_HAD_TRANSFER_MAX-1-count;char extra;
            int n=esp_http_client_read(client,space?(char *)raw+count:&extra,(int)(space?space:1));
            if(n<0 || (uint32_t)n>(space?space:1) || (!space && n))goto close;
            count+=(uint32_t)n;atomic_store(&job->bytes,count);
            if(!budget(job,client,deadline,true))goto close;
            if(esp_http_client_is_complete_data_received(client))break;
            if(!n)goto close;
        }
        if(declared>0 && (uint64_t)declared!=count)goto close;
        raw[count]=0;ok=true;
close:
        esp_http_client_cleanup(client); // Header callback/context is gone before this stack frame ends.
        if(again){memcpy(current,next,sizeof current);continue;}
        break;
    }
    if(ok && !atomic_load(&job->cancel)) {
        if(job->kind==2)ok=am_had_decode(job,raw,count);
        else if(count>=2 && raw[0]==0x1f && raw[1]==0x8b)ok=am_had_inflate(job,raw,count);
        else {job->data=raw;job->length=count;raw=NULL;}
    } else ok=false;
    heap_caps_free(raw);
    if(!ok && !job->error[0])snprintf(job->error,sizeof job->error,"Incomplete feed or image.");
    return ok;
}
