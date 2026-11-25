// Privileged native-app callbacks. Only the Home task invokes module code or draws;
// the host drawer owns submitted fb until ui_frame returns. This is not a sandbox.
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_memory_utils.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "badge/badge.h" // SDK headers precede the display's H macro.
#include "badge/font8x8_basic.h"
#include "badge_native_api.h"
#include "native_host.h"
#include "managed_apps.h"
#include "badge_video_api.h"
#include "native_video.h"
#include "native_codex_private.h"
#include "native_hackaday_private.h"
#include "native_messages_private.h"
#include "native_doom_private.h"
#include "native_host_private.h"

enum { STOP_MS=1000, WAIT_MAX_MS=3000, EXIT_MS=600, TEXT_MAX=64 };
_Static_assert(BTN_PLUS==0 && BTN_MINUS==1 && BTN_A==2 && BTN_B==3,"Native physical button bits");
_Static_assert((uint32_t)BADGE_VIDEO_CLIP_BYTES==(uint32_t)AM_VIDEO_CLIP_BYTES,"Video asset binding");
static struct {
    atomic_bool active, stopping, bad;
    _Atomic(TaskHandle_t) owner;
    TaskHandle_t worker;
    SemaphoreHandle_t frame, stopped;
    bool lease, submitted, acknowledged, input_seen, frame_seen;
    bool chord_down, exit_requested, exit_observed, lower_exit;
    uint32_t chord_since;
    bool video_mode;
    am_native_video_t video;
    bool codex_mode,codex_closed_ok;
    bool had_mode,had_closed_ok;
    bool messages_mode,messages_closed_ok;bool doom_mode;
} host;

static bool allowed(void *context)
{
    if (atomic_load(&host.active) && context==&host &&
        xTaskGetCurrentTaskHandle()==atomic_load(&host.owner) && !atomic_load(&host.stopping) &&
        !(host.codex_mode && host.codex_closed_ok) && !(host.had_mode && host.had_closed_ok) &&
        !(host.messages_mode && host.messages_closed_ok)) return true;
    if (atomic_load(&host.active)) atomic_store(&host.bad,true);
    return false;
}
static bool drawing(void *context)
{
    if (!allowed(context)) return false;
    if (host.lease) return true;
    atomic_store(&host.bad,true);
    return false;
}
void am_native_fault(void) { atomic_store(&host.bad,true); }
bool am_native_access(void *ctx,unsigned role,bool frame)
{
    if((role==AM_NATIVE_CODEX && host.codex_mode) || (role==AM_NATIVE_HACKADAY && host.had_mode) ||
       (role==AM_NATIVE_MESSAGES && host.messages_mode) || (role==AM_NATIVE_DOOM && host.doom_mode))
        return frame?drawing(ctx):allowed(ctx);
    am_native_fault();return false;
}
void am_native_note_release(unsigned role)
{ if(role==AM_NATIVE_CODEX)host.codex_closed_ok=true;else if(role==AM_NATIVE_HACKADAY)host.had_closed_ok=true;
  else if(role==AM_NATIVE_MESSAGES)host.messages_closed_ok=true; }
static uint32_t clock_ms(void) { return (uint32_t)(esp_timer_get_time()/1000); }
static void exit_poll(uint32_t held)
{
    if(!host.lower_exit && !host.doom_mode)return; // Preserve Asteroids thrust/fire and Doom lower-button Use.
    uint32_t mask=host.doom_mode?(NATIVE_PLUS|NATIVE_MINUS):(NATIVE_A|NATIVE_B);
    if ((held&mask)!=mask) host.chord_down=false;
    else if (!host.chord_down) host.chord_down=true,host.chord_since=clock_ms();
    else if ((uint32_t)(clock_ms()-host.chord_since)>=EXIT_MS) host.exit_requested=true;
}
int32_t am_native_key(void *ctx,uint32_t timeout,bool exit_on_b,bool force_exit)
{
    if(!allowed(ctx))return -1;
    if(timeout>1000){am_native_fault();return -1;}
    exit_poll((uint32_t)btn_held());
    int key=host.exit_requested || force_exit?-1:btn_get((int)timeout);host.input_seen=true;
    exit_poll((uint32_t)btn_held());
    if(exit_on_b && key==BTN_B)host.exit_requested=host.exit_observed=true;
    if(!exit_on_b && (host.exit_requested || force_exit)) {
        host.exit_requested=host.exit_observed=true;return BADGE_HAD_EXIT;
    }
    return key>=BTN_PLUS && key<=BTN_B?key:-1;
}
static uint32_t native_now(void *context) { return allowed(context)?clock_ms():0; }
static void native_wait(void *context,uint32_t delay)
{
    if (!allowed(context)) return;
    if (delay>WAIT_MAX_MS) { atomic_store(&host.bad,true);return; }
    uint32_t start=clock_ms();
    for (;;) {
        exit_poll((uint32_t)btn_held()); // MENU Home remains the OS's immediate restart path.
        uint32_t elapsed=clock_ms()-start;
        if (host.exit_requested || elapsed>=delay) return;
        uint32_t remaining=delay-elapsed;
        TickType_t ticks=pdMS_TO_TICKS(remaining<20?remaining:20);
        vTaskDelay(ticks?ticks:1);
    }
}
static bool writable_input(badge_native_input_t *out)
{
    uintptr_t first=(uintptr_t)out;
    if (!first || first%_Alignof(badge_native_input_t) || first>UINTPTR_MAX-(sizeof *out-1)) return false;
    const void *last=(const void *)(first+sizeof *out-1);
    return esp_ptr_byte_accessible(out) && esp_ptr_byte_accessible(last) &&
        ((esp_ptr_in_dram(out) && esp_ptr_in_dram(last)) ||
         (esp_ptr_external_ram(out) && esp_ptr_external_ram(last)));
}
static void native_input(void *context,badge_native_input_t *out)
{
    if (!allowed(context)) return;
    if (!writable_input(out)) { atomic_store(&host.bad,true);return; }
    uint32_t held=(uint32_t)btn_held()&15u,pressed=0;
    for (unsigned i=0;i<16;i++) {
        int key=btn_get(0);
        if (key<0) break;
        if (key<4) pressed|=1u<<key;
    }
    exit_poll(held);
    if(host.video_mode && (pressed&NATIVE_B))host.exit_requested=true;
    int knob=knob_angle();
    *out=(badge_native_input_t){held,pressed,knob>=0 && knob<4096?knob:-1,host.exit_requested};
    host.input_seen=true;
    if (out->exit_requested) host.exit_observed=true;
}
static uint32_t native_random(void *context) { return allowed(context)?esp_random():0; }
static int32_t native_begin(void *context)
{
    if (!allowed(context)) return 0;
    if (host.lease) { atomic_store(&host.bad,true);return 0; }
    if (xSemaphoreTake(host.frame,0)!=pdTRUE) return 0;
    if (host.submitted) { xSemaphoreGive(host.frame);return 0; }
    host.lease=true;
    if(host.had_mode)am_had_apply_keys();
    return 1;
}
static void native_submit(void *context)
{
    if (!drawing(context)) return;
    host.submitted=true;host.frame_seen=true;host.lease=false;
    xSemaphoreGive(host.frame);
    xTaskNotifyGive(host.worker);
}
static void clipped_box(int64_t x,int64_t y,int64_t w,int64_t h,bool ink)
{
    if (w<=0 || h<=0) return;
    int64_t right=x+w,bottom=y+h;
    if (x<0) x=0;
    if (y<TOP) y=TOP;
    if (right>W) right=W;
    if (bottom>H) bottom=H;
    if (x<right && y<bottom) box((int)x,(int)y,(int)(right-x),(int)(bottom-y),ink);
}
static void native_clear(void *context)
{ if (drawing(context)) clipped_box(0,TOP,W,H-TOP,false); }
static void native_box(void *context,int32_t x,int32_t y,int32_t w,int32_t h,uint32_t ink)
{ if (drawing(context)) clipped_box(x,y,w,h,ink!=0); }
static void native_outline(void *context,int32_t x,int32_t y,int32_t w,int32_t h,int32_t t,uint32_t ink)
{
    if (!drawing(context) || w<=0 || h<=0 || t<=0) return;
    int64_t tw=t<w?t:w,th=t<h?t:h;
    clipped_box(x,y,w,th,ink!=0);clipped_box(x,(int64_t)y+h-th,w,th,ink!=0);
    clipped_box(x,y,tw,h,ink!=0);clipped_box((int64_t)x+w-tw,y,tw,h,ink!=0);
}
static bool readable_byte(const void *p)
{
    // IDF's byte-accessible predicate covers RAM, not ordinary flash DROM.
    return esp_ptr_in_drom(p) ||
        (esp_ptr_byte_accessible(p) && (esp_ptr_in_dram(p) || esp_ptr_external_ram(p)));
}
static void native_text(void *context,int32_t x,int32_t y,int32_t scale,const char *s,uint32_t ink)
{
    if (!drawing(context)) return;
    char copy[TEXT_MAX+1];
    if (scale<1 || scale>4 || !s) { atomic_store(&host.bad,true);return; }
    size_t length=0;
    for (;;) {
        uintptr_t address=(uintptr_t)s;
        if (address>UINTPTR_MAX-length || !readable_byte((const void *)(address+length))) break;
        copy[length]=*(const char *)(address+length);
        if (!copy[length]) {
            for (size_t k=0;k<length;k++) for (int row=0;row<8;row++) for (int col=0;col<8;col++)
                if ((font8x8_basic[(unsigned char)copy[k]&127][row]>>col)&1)
                    clipped_box((int64_t)x+(int64_t)(k*8+col)*scale,(int64_t)y+row*scale,scale,scale,ink!=0);
            return;
        }
        if (length==TEXT_MAX) break;
        length++;
    }
    atomic_store(&host.bad,true);
}
static bool drain(void *context)
{
    if (!allowed(context)) return false;
    if (host.lease) { atomic_store(&host.bad,true);return false; }
    for (;;) {
        xSemaphoreTake(host.frame,portMAX_DELAY);
        if (!host.submitted) return true; // Caller keeps the mutex through close/wipe.
        xSemaphoreGive(host.frame);
        vTaskDelay(1);
    }
}
static void native_idle(void *context)
{ if (drain(context)) { ui_close();xSemaphoreGive(host.frame); } }
int32_t am_native_info(void *ctx,unsigned role,int32_t (*show)(void *),void *owner)
{
    if(!show || !am_native_access(ctx,role,false) || !drain(ctx))return 0;
    int32_t result=show(owner);xSemaphoreGive(host.frame);return result;
}
static void native_wipe(void *context)
{ if (drain(context)) { ui_wipe();xSemaphoreGive(host.frame); } }

static bool writable_span(const void *p,size_t n)
{
    uintptr_t first=(uintptr_t)p;
    if(!first || !n || first>UINTPTR_MAX-(n-1))return false;
    const void *last=(const void *)(first+n-1);
    return esp_ptr_byte_accessible(p) && esp_ptr_byte_accessible(last) &&
        ((esp_ptr_in_dram(p) && esp_ptr_in_dram(last)) || (esp_ptr_external_ram(p) && esp_ptr_external_ram(last)));
}
static bool video_allowed(void *ctx)
{
    if(!allowed(ctx) || !host.video_mode)return false;
    if(am_video_asset_live(&host.video.asset))return true;
    atomic_store(&host.bad,true);return false;
}
static uint64_t video_now(void *ctx)
{ return video_allowed(ctx)?(uint64_t)esp_timer_get_time():0; }
static void video_wait(void *ctx,uint32_t us)
{
    if(!video_allowed(ctx))return;
    if(us>3000000){atomic_store(&host.bad,true);return;}
    uint64_t start=(uint64_t)esp_timer_get_time();
    for(;;) {
        uint64_t elapsed=(uint64_t)esp_timer_get_time()-start;
        if(elapsed>=us || host.exit_requested)break;
        uint64_t left=us-elapsed;
        TickType_t ticks=pdMS_TO_TICKS((left>20000?20000:left+999)/1000);
        vTaskDelay(ticks?ticks:1);
    }
}
static uint32_t video_size(void *ctx,uint32_t index)
{
    if(!video_allowed(ctx) || index!=BADGE_VIDEO_CLIP)return 0;
    uint8_t byte;
    return am_video_asset_read(&host.video.asset,0,&byte,1)==AM_OK?BADGE_VIDEO_CLIP_BYTES:0;
}
static int32_t video_read(void *ctx,uint32_t index,uint32_t offset,uint8_t *out,uint32_t n)
{
    if(!video_allowed(ctx))return 0;
    if(index!=BADGE_VIDEO_CLIP || n>AM_ASSET_CHUNK || !writable_span(out,n)) {
        atomic_store(&host.bad,true);return 0;
    }
    am_result_t r=am_video_asset_read(&host.video.asset,offset,out,n);
    if(r!=AM_OK)atomic_store(&host.bad,true);
    return r==AM_OK;
}
static int32_t video_inflate(void *ctx,uint8_t *out,uint32_t n,const uint8_t *in,uint32_t len)
{
    if(!video_allowed(ctx))return -1;
    if(n!=BADGE_VIDEO_PIXELS || !len || len>BADGE_VIDEO_COMPRESSED_MAX ||
       !writable_span(out,n) || !writable_span(in,len)) { atomic_store(&host.bad,true);return -1; }
    int32_t r=host.video.inflate(out,n,in,len);
    if(r!=(int32_t)n)atomic_store(&host.bad,true);
    return r;
}
static int32_t video_blit(void *ctx,int32_t x,int32_t y,uint32_t w,uint32_t h,const uint8_t *bits,uint32_t n)
{
    if(!video_allowed(ctx) || !drawing(ctx))return 0;
    if(x!=(W-BADGE_VIDEO_WIDTH)/2 || y!=TOP || w!=BADGE_VIDEO_WIDTH || h!=BADGE_VIDEO_HEIGHT ||
       n!=BADGE_VIDEO_PIXELS || !writable_span(bits,n)) { atomic_store(&host.bad,true);return 0; }
    for(uint32_t row=0;row<h;row++)
        for(uint32_t col=0;col<w/8;col++)fb[(y+row)*(W/8)+x/8+col]=bits[row*(w/8)+col];
    return 1;
}
static int32_t video_info(void *ctx)
{
    if(!video_allowed(ctx) || !drain(ctx))return 0;
    bool shown=host.video.info();
    ui_keys("Info",NULL,"Restart","Home");
    if(shown){ui_frame(1);ui_close();}
    xSemaphoreGive(host.frame);return shown;
}
#define BASE_API(size_) \
    .abi_version=BADGE_NATIVE_ABI,.struct_size=(size_),.context=&host,.width=W,.height=H,.top=TOP, \
    .now_ms=native_now,.wait_ms=native_wait,.input=native_input,.random=native_random, \
    .begin_frame=native_begin,.submit_frame=native_submit,.clear=native_clear, \
    .box=native_box,.outline=native_outline,.text=native_text,.frame_idle=native_idle,.wipe=native_wipe
static const badge_native_api_t api={BASE_API(sizeof(badge_native_api_t))};
static const badge_video_api_t video_api={
    .base={BASE_API(sizeof(badge_video_api_t))},.now_us=video_now,.wait_us=video_wait,
    .asset_size=video_size,.asset_read=video_read,.inflate=video_inflate,.blit=video_blit,.info=video_info
};
const badge_native_api_t *badge_app_get_api(uint32_t abi,uint32_t minimum_size)
{
    if (!allowed(&host)) return NULL;
    if(host.doom_mode && abi==BADGE_NATIVE_ABI) {
        const badge_native_api_t *selected=am_doom_get(minimum_size);
        if(selected)return selected;
        am_native_fault();return NULL;
    }
    if(host.messages_mode && abi==BADGE_NATIVE_ABI) {
        const badge_native_api_t *selected=am_messages_get(minimum_size);
        if(selected)return selected;
        am_native_fault();return NULL;
    }
    if(host.had_mode && abi==BADGE_NATIVE_ABI) {
        const badge_native_api_t *selected=am_had_get(minimum_size);
        if(selected)return selected;
        am_native_fault();return NULL;
    }
    if(host.codex_mode && abi==BADGE_NATIVE_ABI) {
        const badge_native_api_t *selected=am_codex_get(minimum_size);
        if(selected)return selected;
        am_codex_fault();return NULL;
    }
    if(host.video_mode && abi==BADGE_NATIVE_ABI && minimum_size<=sizeof video_api)return &video_api.base;
    if (abi==BADGE_NATIVE_ABI && minimum_size<=sizeof api) return &api;
    atomic_store(&host.bad,true);
    return NULL;
}
static void draw_task(void *unused)
{
    (void)unused;
    for (;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        xSemaphoreTake(host.frame,portMAX_DELAY);
        if (host.submitted) { ui_frame(1);host.submitted=false; }
        bool stop=atomic_load(&host.stopping);
        xSemaphoreGive(host.frame);
        if (stop) {
            xSemaphoreGive(host.stopped);
            // Last host-resource access is the acknowledgment. Parent deletes only
            // after this task has suspended AND left every core; never self-delete.
            for (;;) vTaskSuspend(NULL);
        }
    }
}
bool am_native_host_live(void) { return atomic_load(&host.active); }
bool am_native_host_good_return(void)
{ return !atomic_load(&host.bad) && !host.lease && host.input_seen && host.frame_seen && host.exit_observed &&
         (!host.codex_mode || host.codex_closed_ok) && (!host.had_mode || host.had_closed_ok) &&
         (!host.messages_mode || host.messages_closed_ok); }
am_result_t am_native_host_start(void)
{ return am_native_host_start_for("aliens"); }
static am_result_t start_host(const char *id,const am_native_video_t *video,const am_native_codex_t *codex,
                               const am_native_hackaday_t *had,const am_native_messages_t *messages,const am_native_doom_t *doom)
{
    if (am_native_host_live()) return AM_BUSY;
    if(!id || (!video && !codex && !had && !messages && !doom && strcmp(id,"aliens") && strcmp(id,"rocks")))return AM_DISABLED;
    host.doom_mode=doom!=NULL;host.messages_mode=messages!=NULL;host.messages_closed_ok=false;
    host.had_mode=had!=NULL;host.had_closed_ok=false;
    host.codex_mode=codex!=NULL;host.codex_closed_ok=false;
    host.video_mode=video!=NULL;
    host.video=video?*video:(am_native_video_t){0};
    host.lower_exit=!strcmp(id,"aliens") || had;
    host.lease=host.submitted=host.acknowledged=host.input_seen=host.frame_seen=false;
    host.chord_down=host.exit_requested=host.exit_observed=false;
    atomic_store(&host.bad,false);atomic_store(&host.stopping,false);
    if(codex){am_result_t r=am_codex_bind(codex,&api);if(r!=AM_OK)return r;}
    if(had){am_result_t r=am_had_bind(had,&api);if(r!=AM_OK)return r;}
    if(messages){am_result_t r=am_messages_bind(messages,&api);if(r!=AM_OK)return r;}
    if(doom){am_result_t r=am_doom_bind(doom,&api);if(r!=AM_OK)return r;}
    host.frame=xSemaphoreCreateMutex();host.stopped=xSemaphoreCreateBinary();
    if (!host.frame || !host.stopped) {
        if (host.frame) vSemaphoreDelete(host.frame);
        if (host.stopped) vSemaphoreDelete(host.stopped);
        host.frame=host.stopped=NULL;
        host.video=(am_native_video_t){0};host.video_mode=false;
        if(codex)am_codex_end();
        if(had)am_had_end();
        if(messages)am_messages_end();
        if(doom)am_doom_end();
        return AM_FULL;
    }
    atomic_store(&host.owner,xTaskGetCurrentTaskHandle());
    if (xTaskCreate(draw_task,"native draw",6144,NULL,5,&host.worker)!=pdPASS) {
        vSemaphoreDelete(host.frame);vSemaphoreDelete(host.stopped);
        host.frame=host.stopped=NULL;host.worker=NULL;
        host.video=(am_native_video_t){0};host.video_mode=false;
        if(codex)am_codex_end();
        if(had)am_had_end();
        if(messages)am_messages_end();
        if(doom)am_doom_end();
        return AM_FULL;
    }
    btn_flush();
    if(had || messages)ui_keys(NULL,NULL,NULL,NULL);
    else if(codex)ui_keys(NULL,NULL,"Retry","Home");
    else if(doom)ui_keys("LEFT","RIGHT","WALK","FIRE");
    else if(host.video_mode)ui_keys("Info",NULL,"Restart","Home");
    else if(host.lower_exit)ui_keys("FIRE","FIRE","LEFT","RIGHT");
    else ui_keys("Turn L","Turn R","Thrust","Fire");
    atomic_store(&host.active,true);
    return AM_OK;
}
am_result_t am_native_host_start_for(const char *id)
{ return start_host(id,NULL,NULL,NULL,NULL,NULL); }
am_result_t am_native_host_start_codex(const am_native_codex_t *codex)
{ return codex?start_host("codex_board",NULL,codex,NULL,NULL,NULL):AM_DISABLED; }
am_result_t am_native_host_start_hackaday(const am_native_hackaday_t *had)
{ return had?start_host("hackaday",NULL,NULL,had,NULL,NULL):AM_DISABLED; }
am_result_t am_native_host_start_messages(const am_native_messages_t *messages)
{ return messages?start_host("messaging",NULL,NULL,NULL,messages,NULL):AM_DISABLED; }
am_result_t am_native_host_start_doom(const am_native_doom_t *doom)
{ return doom?start_host("doom",NULL,NULL,NULL,NULL,doom):AM_DISABLED; }
am_result_t am_native_host_start_video(const am_native_video_t *video)
{
    if(am_native_host_live())return AM_BUSY;
    if(!video || !video->inflate || !video->info)return AM_DISABLED;
    uint8_t header[12];am_result_t r=am_video_asset_read(&video->asset,0,header,sizeof header);
    if(r!=AM_OK)return r;
    if(header[0]!='V' || header[1]!='I' || header[2]!='D' || header[3]!='2')return AM_BAD_RECORD;
    return start_host("video",video,NULL,NULL,NULL,NULL);
}
static bool worker_parked(void)
{
    if (eTaskGetState(host.worker)!=eSuspended) return false;
    for (BaseType_t core=0;core<configNUMBER_OF_CORES;core++)
        if (xTaskGetCurrentTaskHandleForCore(core)==host.worker) return false;
    return true;
}
am_result_t am_native_host_stop(void)
{
    if (!am_native_host_live()) return AM_OK;
    if (xTaskGetCurrentTaskHandle()!=atomic_load(&host.owner)) return AM_BUSY;
    if (!atomic_load(&host.stopping)) {
        if (host.lease) {
            atomic_store(&host.bad,true);host.lease=false;xSemaphoreGive(host.frame);
        }
        atomic_store(&host.stopping,true);
        xTaskNotifyGive(host.worker);
    }
    if (!host.acknowledged) {
        if (xSemaphoreTake(host.stopped,pdMS_TO_TICKS(STOP_MS))!=pdTRUE) return AM_BUSY;
        host.acknowledged=true;
    }
    TickType_t start=xTaskGetTickCount();
    while (!worker_parked()) {
        if ((TickType_t)(xTaskGetTickCount()-start)>=pdMS_TO_TICKS(STOP_MS)) return AM_BUSY;
        vTaskDelay(1);
    }
    if(host.had_mode){am_result_t r=am_had_end();if(r!=AM_OK)return r;}
    if(host.messages_mode){am_result_t r=am_messages_end();if(r!=AM_OK)return r;}
    vTaskDelete(host.worker);host.worker=NULL;
    if(host.doom_mode)am_doom_end();
    ui_close(); // Drawer is gone, including its last in-flight panel submission.
    vSemaphoreDelete(host.frame);vSemaphoreDelete(host.stopped);
    host.frame=host.stopped=NULL;
    atomic_store(&host.active,false);
    if(host.codex_mode)am_codex_end();
    host.video=(am_native_video_t){0};host.video_mode=false;
    return AM_OK;
}
