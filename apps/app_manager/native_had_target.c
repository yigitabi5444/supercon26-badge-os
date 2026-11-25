// One resident host job at a time. Never execute or retain module code/data on it.
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "badge/badge.h"
#include "package_http.h"
#include "native_had_job.h"
#include "native_had_url.h"

static struct {
    am_had_job_t job;
    TaskHandle_t worker;SemaphoreHandle_t stopped;
    atomic_bool radio;
    bool active,acknowledged;
} reader;
static bool cancelled(void *ctx)
{ return atomic_load(&((am_had_job_t *)ctx)->cancel); }
static void worker(void *unused)
{
    (void)unused;am_had_job_t *job=&reader.job;bool ok=false;
    if(job->kind==1) {
        atomic_store(&reader.radio,true);
        int64_t start=esp_timer_get_time();bool joined=wifi_connected() || wifi_connect(20000);
        uint32_t ms=(uint32_t)((esp_timer_get_time()-start)/1000);if(ms>20000)ms=20000;
        atomic_store(&job->join_ms,ms);atomic_store(&job->joined,joined);
        if(!joined)snprintf(job->error,sizeof job->error,"Could not join the badge Wi-Fi.");
        else if(am_https_link_ready(cancelled,job,NULL,20000)!=AM_OK)
            snprintf(job->error,sizeof job->error,"Could not sync time for HTTPS.");
        else if(!cancelled(job))ok=am_had_fetch_job(job);
    } else {
        atomic_store(&job->joined,wifi_connected());
        if(atomic_load(&job->joined) && !cancelled(job))ok=am_had_fetch_job(job);
    }
    if(cancelled(job)) {
        heap_caps_free(job->data);job->data=NULL;job->length=0;ok=false;
        snprintf(job->error,sizeof job->error,"Download cancelled.");
    }
    atomic_store_explicit(&job->status,ok?BADGE_HAD_OK:BADGE_HAD_FAILED,memory_order_release);
    xSemaphoreGive(reader.stopped);
    // Parent joins after both acknowledgment and suspension/off-core. No self-delete.
    for(;;)vTaskSuspend(NULL);
}
static bool parked(void)
{
    if(eTaskGetState(reader.worker)!=eSuspended)return false;
    for(BaseType_t core=0;core<configNUMBER_OF_CORES;core++)
        if(xTaskGetCurrentTaskHandleForCore(core)==reader.worker)return false;
    return true;
}
static int32_t end_job(void *ctx,uint32_t close_link)
{
    (void)ctx;
    if(reader.worker) {
        atomic_store(&reader.job.cancel,true);TickType_t start=xTaskGetTickCount();
        if(!reader.acknowledged) {
            if(xSemaphoreTake(reader.stopped,pdMS_TO_TICKS(1000))!=pdTRUE)return 0;
            reader.acknowledged=true;
        }
        while(!parked()) {
            if((TickType_t)(xTaskGetTickCount()-start)>=pdMS_TO_TICKS(1000))return 0;
            vTaskDelay(1);
        }
        vTaskDelete(reader.worker);reader.worker=NULL;
        vSemaphoreDelete(reader.stopped);reader.stopped=NULL;reader.acknowledged=false;
        heap_caps_free(reader.job.data);reader.job.data=NULL;reader.job.length=0;reader.job.kind=0;
    }
    if(close_link && atomic_load(&reader.radio)) {
        wifi_off();ranging_start();atomic_store(&reader.radio,false);
    }
    return 1;
}
static int32_t begin_job(uint32_t kind,const char *url,uint32_t w,uint32_t h)
{
    if(!reader.active || reader.worker)return 0;
    memset(&reader.job,0,sizeof reader.job);
    atomic_init(&reader.job.cancel,false);atomic_init(&reader.job.status,BADGE_HAD_PENDING);
    atomic_init(&reader.job.joined,false);atomic_init(&reader.job.join_ms,0);atomic_init(&reader.job.bytes,0);
    reader.job.kind=kind;reader.job.box_w=w;reader.job.box_h=h;
    snprintf(reader.job.url,sizeof reader.job.url,"%s",url);
    reader.stopped=xSemaphoreCreateBinary();if(!reader.stopped)return 0;
    if(xTaskCreate(worker,"had net",20480,NULL,4,&reader.worker)!=pdPASS) {
        vSemaphoreDelete(reader.stopped);reader.stopped=NULL;reader.worker=NULL;return 0;
    }
    return 1;
}
static am_result_t begin(void *ctx)
{
    (void)ctx;
    if(reader.active || reader.worker)return AM_BUSY;
    if(!am_https_enabled())return AM_DISABLED;
    reader.active=true;return AM_OK;
}
static void *allocate(void *ctx,uint32_t n)
{ (void)ctx;return heap_caps_calloc(1,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
static void deallocate(void *ctx,void *p) { (void)ctx;heap_caps_free(p); }
static int32_t begin_feed(void *ctx)
{ (void)ctx;return begin_job(1,"https://hackaday.com/blog/feed/",0,0); }
static int32_t begin_image(void *ctx,const char *url,uint32_t w,uint32_t h)
{
    (void)ctx;char host[254];const char *path;bool tls;
    if(!url || !w || !h || w>960 || h>544 || !atomic_load(&reader.radio) ||
       !am_had_url(url,false,host,&path,&tls))return 0;
    return begin_job(2,url,w,h);
}
static int32_t poll(void *ctx,badge_had_result_t *out)
{
    (void)ctx;*out=(badge_had_result_t){0};if(!reader.worker)return BADGE_HAD_FAILED;
    am_had_job_t *job=&reader.job;uint32_t status=atomic_load_explicit(&job->status,memory_order_acquire);
    out->wifi_joined=atomic_load(&job->joined);out->join_ms=atomic_load(&job->join_ms);out->bytes=atomic_load(&job->bytes);
    if(status!=BADGE_HAD_PENDING) {
        out->data=job->data;out->length=job->length;out->width=job->width;out->height=job->height;
        memcpy(out->error,job->error,sizeof out->error);out->error[63]=0;
    }
    return (int32_t)status;
}
static am_result_t finish(void *ctx)
{
    if(end_job(ctx,1)!=1)return AM_BUSY;
    memset(reader.job.url,0,sizeof reader.job.url);reader.active=false;return AM_OK;
}
const am_native_hackaday_t am_native_hackaday_target={.begin=begin,.allocate=allocate,.deallocate=deallocate,
    .begin_feed=begin_feed,.begin_image=begin_image,.poll=poll,.end_job=end_job,.finish=finish};
