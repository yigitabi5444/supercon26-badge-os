// Threaded FreeRTOS/display doubles: actual native_host.c owns the protocol.
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_memory_utils.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "badge/badge.h"
#include "native_host_double.h"
#include "badge/font8x8_basic.h"

struct native_test_task {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    atomic_int state;
    unsigned notifications;
    bool deleted;
    void (*entry)(void *);
    void *argument;
};
struct native_test_sem {
    pthread_mutex_t lock;
    pthread_cond_t wake;
    unsigned value;
    bool mutex;
    TaskHandle_t holder;
};
static struct native_test_task main_task,foreign_task;
static _Thread_local TaskHandle_t current;
static _Atomic(TaskHandle_t) worker;
static _Atomic(TaskHandle_t) net_worker;
static pthread_t main_thread;
static SemaphoreHandle_t frame_sem;
static atomic_bool panel_active;
static int keys[32];
static unsigned key_count,key_next;
static struct { uintptr_t begin,end;unsigned kind;bool bytes; } memory[256];
static unsigned memory_count,sem_attempts;
static pthread_mutex_t memory_lock=PTHREAD_MUTEX_INITIALIZER;
uint8_t fb[PLANE]={0};
atomic_uint nh_held,nh_frames_started,nh_frames_done,nh_closes,nh_wipes,nh_boxes;
atomic_uint nh_sem_created,nh_sem_deleted,nh_tasks_created,nh_tasks_deleted;
atomic_uint_fast64_t nh_time_ms;
atomic_bool nh_panel_blocked,nh_notify_paused,nh_suspend_paused,nh_force_on_core;
unsigned nh_fail_sem,nh_fail_task,nh_flushed;
unsigned nh_auto_b_frame;
unsigned nh_auto_b_poll,nh_button_polls;
unsigned nh_auto_chord_poll;
unsigned nh_resident_tasks;
void (*nh_resident_entry)(void *);
int nh_knob;
const char *nh_labels[4];

void nh_sleep(void) { struct timespec delay={0,1000000};nanosleep(&delay,NULL); }
void nh_await(atomic_uint *counter,unsigned value)
{
    for (unsigned i=0;i<3000 && atomic_load(counter)<value;i++) nh_sleep();
    assert(atomic_load(counter)>=value);
}
static struct timespec deadline(TickType_t timeout)
{
    struct timespec end;assert(!clock_gettime(CLOCK_REALTIME,&end));
    end.tv_sec+=timeout/1000;end.tv_nsec+=(long)(timeout%1000)*1000000;
    if (end.tv_nsec>=1000000000) { end.tv_sec++;end.tv_nsec-=1000000000; }
    return end;
}
void nh_reset(void)
{
    assert(!atomic_load(&worker) && !atomic_load(&net_worker));assert(atomic_load(&nh_sem_created)==atomic_load(&nh_sem_deleted));
    assert(atomic_load(&nh_tasks_created)==atomic_load(&nh_tasks_deleted));
    main_thread=pthread_self();current=NULL;frame_sem=NULL;memory_count=key_count=key_next=sem_attempts=0;
    nh_fail_sem=nh_fail_task=nh_flushed=0;nh_knob=-1;
    nh_auto_b_frame=0;
    nh_auto_b_poll=nh_button_polls=0;
    nh_auto_chord_poll=0;
    nh_resident_tasks=0;nh_resident_entry=NULL;
    atomic_store(&nh_held,0);atomic_store(&nh_time_ms,0);atomic_store(&panel_active,false);
    atomic_store(&nh_frames_started,0);atomic_store(&nh_frames_done,0);
    atomic_store(&nh_closes,0);atomic_store(&nh_wipes,0);atomic_store(&nh_boxes,0);
    atomic_store(&nh_sem_created,0);atomic_store(&nh_sem_deleted,0);
    atomic_store(&nh_tasks_created,0);atomic_store(&nh_tasks_deleted,0);
    atomic_store(&nh_panel_blocked,false);atomic_store(&nh_notify_paused,false);
    atomic_store(&nh_suspend_paused,false);atomic_store(&nh_force_on_core,false);
    memset(fb,0,sizeof fb);
}
void nh_memory(const void *p,size_t size,unsigned kind,bool bytes)
{
    assert(!pthread_mutex_lock(&memory_lock));
    assert(memory_count<256 && size && (uintptr_t)p<=UINTPTR_MAX-size);
    memory[memory_count].begin=(uintptr_t)p;memory[memory_count].end=(uintptr_t)p+size;
    memory[memory_count].kind=kind;memory[memory_count++].bytes=bytes;
    assert(!pthread_mutex_unlock(&memory_lock));
}
void nh_forget(const void *p)
{
    assert(!pthread_mutex_lock(&memory_lock));
    for(unsigned i=0;i<memory_count;i++)if(memory[i].begin==(uintptr_t)p) {
        memory[i]=memory[--memory_count];break;
    }
    assert(!pthread_mutex_unlock(&memory_lock));
}
static unsigned memory_kind(const void *p,bool bytes)
{
    assert(!pthread_mutex_lock(&memory_lock));unsigned result=0;
    for (unsigned i=0;i<memory_count;i++)
        if ((uintptr_t)p>=memory[i].begin && (uintptr_t)p<memory[i].end) {
            result=!bytes || memory[i].bytes?memory[i].kind:0;break;
        }
    assert(!pthread_mutex_unlock(&memory_lock));return result;
}
bool esp_ptr_byte_accessible(const void *p) { return memory_kind(p,true)!=0; }
bool esp_ptr_in_dram(const void *p) { return memory_kind(p,false)==NH_DRAM; }
bool esp_ptr_external_ram(const void *p) { return memory_kind(p,false)==NH_PSRAM; }
bool esp_ptr_in_drom(const void *p) { return memory_kind(p,false)==NH_DROM; }
int64_t esp_timer_get_time(void) { return (int64_t)atomic_load(&nh_time_ms)*1000; }
uint32_t esp_random(void) { return 0x12345678; }
TaskHandle_t xTaskGetCurrentTaskHandle(void)
{ return current?current:pthread_equal(pthread_self(),main_thread)?&main_task:&foreign_task; }
TaskHandle_t xTaskGetCurrentTaskHandleForCore(BaseType_t core)
{
    TaskHandle_t task=atomic_load(&worker);
    if (core==0) return &main_task;
    TaskHandle_t net=atomic_load(&net_worker);
    if(net && atomic_load(&net->state)==eRunning)return net;
    return task && (atomic_load(&nh_force_on_core) || atomic_load(&task->state)==eRunning)?task:NULL;
}
TickType_t xTaskGetTickCount(void) { return (TickType_t)atomic_load(&nh_time_ms); }
void vTaskDelay(TickType_t ticks) { assert(ticks);atomic_fetch_add(&nh_time_ms,ticks);nh_sleep(); }
static void *task_main(void *argument)
{
    TaskHandle_t task=argument;current=task;atomic_store(&task->state,eRunning);
    task->entry(task->argument);assert(!"Worker must park, not return");return NULL;
}
BaseType_t xTaskCreate(void (*entry)(void *),const char *name,uint32_t stack,void *arg,UBaseType_t priority,TaskHandle_t *out)
{
    assert(name);
    if(!strcmp(name,"msg-dns")) {
        assert(stack==3072 && priority==2 && !arg && !out);nh_resident_tasks++;nh_resident_entry=entry;return pdPASS;
    }
    assert(out);bool network=!strcmp(name,"had net");
    assert(network?(stack==20480 && priority==4):(stack>=6144 && priority==5));
    if (nh_fail_task) return pdFAIL;
    TaskHandle_t task=calloc(1,sizeof *task);assert(task);
    assert(!pthread_mutex_init(&task->lock,NULL));assert(!pthread_cond_init(&task->wake,NULL));
    task->entry=entry;task->argument=arg;
    if(network)atomic_store(&net_worker,task);else atomic_store(&worker,task);
    *out=task;
    assert(!pthread_create(&task->thread,NULL,task_main,task));atomic_fetch_add(&nh_tasks_created,1);return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task)
{
    assert(task && !task->deleted);assert(!pthread_mutex_lock(&task->lock));
    task->notifications++;assert(!pthread_cond_signal(&task->wake));assert(!pthread_mutex_unlock(&task->lock));
}
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t timeout)
{
    assert(clear==pdTRUE && timeout==portMAX_DELAY);TaskHandle_t task=current;assert(task);
    assert(!pthread_mutex_lock(&task->lock));
    while (!task->notifications || atomic_load(&nh_notify_paused)) {
        atomic_store(&task->state,eBlocked);struct timespec end=deadline(10);
        int result=pthread_cond_timedwait(&task->wake,&task->lock,&end);assert(!result || result==ETIMEDOUT);
    }
    unsigned count=task->notifications;task->notifications=0;atomic_store(&task->state,eRunning);
    assert(!pthread_mutex_unlock(&task->lock));return count;
}
void vTaskSuspend(TaskHandle_t task)
{
    assert(!task && current);task=current;
    while (atomic_load(&nh_suspend_paused)) nh_sleep();
    assert(!pthread_mutex_lock(&task->lock));atomic_store(&task->state,eSuspended);
    while (!task->deleted) assert(!pthread_cond_wait(&task->wake,&task->lock));
    assert(!pthread_mutex_unlock(&task->lock));pthread_exit(NULL);
}
eTaskState eTaskGetState(TaskHandle_t task) { return (eTaskState)atomic_load(&task->state); }
void vTaskDelete(TaskHandle_t task)
{
    assert(task && task!=current && atomic_load(&task->state)==eSuspended && !atomic_load(&nh_force_on_core));
    assert(!pthread_mutex_lock(&task->lock));task->deleted=true;
    assert(!pthread_cond_signal(&task->wake));assert(!pthread_mutex_unlock(&task->lock));
    assert(!pthread_join(task->thread,NULL));
    if(atomic_load(&net_worker)==task)atomic_store(&net_worker,NULL);else atomic_store(&worker,NULL);
    assert(!pthread_mutex_destroy(&task->lock));assert(!pthread_cond_destroy(&task->wake));free(task);
    atomic_fetch_add(&nh_tasks_deleted,1);
}
static SemaphoreHandle_t new_sem(bool mutex)
{
    if (++sem_attempts==nh_fail_sem) return NULL;
    SemaphoreHandle_t sem=calloc(1,sizeof *sem);assert(sem);
    assert(!pthread_mutex_init(&sem->lock,NULL));assert(!pthread_cond_init(&sem->wake,NULL));
    sem->mutex=mutex;sem->value=mutex?1:0;
    if (mutex && !frame_sem)frame_sem=sem;
    atomic_fetch_add(&nh_sem_created,1);return sem;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return new_sem(true); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return new_sem(false); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem,TickType_t timeout)
{
    assert(sem);assert(!pthread_mutex_lock(&sem->lock));struct timespec end=deadline(timeout==portMAX_DELAY?10000:timeout);
    while (!sem->value) {
        if (!timeout) { assert(!pthread_mutex_unlock(&sem->lock));return pdFALSE; }
        int result=pthread_cond_timedwait(&sem->wake,&sem->lock,&end);
        assert(!result || result==ETIMEDOUT);
        if (result==ETIMEDOUT) { assert(timeout!=portMAX_DELAY);assert(!pthread_mutex_unlock(&sem->lock));return pdFALSE; }
    }
    sem->value--;if (sem->mutex) sem->holder=xTaskGetCurrentTaskHandle();
    assert(!pthread_mutex_unlock(&sem->lock));return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem)
{
    assert(sem);assert(!pthread_mutex_lock(&sem->lock));assert(sem->value==0);
    if (sem->mutex) { assert(sem->holder==xTaskGetCurrentTaskHandle());sem->holder=NULL; }
    sem->value++;assert(!pthread_cond_signal(&sem->wake));assert(!pthread_mutex_unlock(&sem->lock));return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t sem)
{
    assert(sem && !sem->holder);if (sem==frame_sem) frame_sem=NULL;
    assert(!pthread_mutex_destroy(&sem->lock));assert(!pthread_cond_destroy(&sem->wake));free(sem);
    atomic_fetch_add(&nh_sem_deleted,1);
}
void box(int x,int y,int width,int height,bool ink)
{
    assert(x>=0 && y>=TOP && width>0 && height>0 && x+width<=W && y+height<=H);
    assert(xTaskGetCurrentTaskHandle()==&main_task && !atomic_load(&panel_active));atomic_fetch_add(&nh_boxes,1);
    for (int row=y;row<y+height;row++) for (int col=x;col<x+width;col++) {
        uint8_t *byte=&fb[row*(W/8)+col/8],mask=(uint8_t)(0x80u>>(col&7));
        if (ink) *byte|=mask;else *byte&=(uint8_t)~mask;
    }
}
void ui_frame(int count)
{
    assert(count==1 && frame_sem && frame_sem->holder==xTaskGetCurrentTaskHandle());
    assert(!atomic_exchange(&panel_active,true));atomic_fetch_add(&nh_frames_started,1);
    while (atomic_load(&nh_panel_blocked)) nh_sleep();
    atomic_store(&panel_active,false);atomic_fetch_add(&nh_frames_done,1);
}
void ui_close(void)
{ assert(xTaskGetCurrentTaskHandle()==&main_task && !atomic_load(&panel_active));atomic_fetch_add(&nh_closes,1); }
void ui_wipe(void)
{ assert(xTaskGetCurrentTaskHandle()==&main_task && !atomic_load(&panel_active));atomic_fetch_add(&nh_wipes,1);memset(fb,0,sizeof fb); }
void ui_keys(const char *p,const char *m,const char *a,const char *b)
{ nh_labels[0]=p;nh_labels[1]=m;nh_labels[2]=a;nh_labels[3]=b; }
int btn_get(int timeout)
{
    assert(timeout==-1 || (timeout>=0 && timeout<=3000));
    if(timeout==-1){atomic_fetch_add(&nh_time_ms,500);atomic_store(&nh_held,0);return BTN_A;}
    if(timeout>0) {
        atomic_fetch_add(&nh_time_ms,(unsigned)timeout);nh_sleep();nh_button_polls++;
        if(nh_auto_chord_poll && nh_button_polls>=nh_auto_chord_poll)
            atomic_store(&nh_held,(1u<<BTN_A)|(1u<<BTN_B));
        if(nh_auto_b_poll && nh_button_polls>=nh_auto_b_poll){nh_auto_b_poll=0;return BTN_B;}
    }
    if(key_next==key_count && nh_auto_b_frame && atomic_load(&nh_frames_started)>=nh_auto_b_frame) {
        nh_auto_b_frame=0;return BTN_B;
    }
    return key_next<key_count?keys[key_next++]:-1;
}
int btn_held(void) { return (int)atomic_load(&nh_held); }
void btn_flush(void) { key_next=key_count;nh_flushed++; }
int knob_angle(void) { return nh_knob; }
void nh_press(int key) { assert(key_count<32);keys[key_count++]=key; }
void text(int x,int y,int scale,const char *s,bool ink)
{
    for(;*s;s++,x+=8*scale)for(int row=0;row<8;row++)for(int col=0;col<8;col++)
        if(font8x8_basic[(unsigned char)*s&127][row]>>col&1)box(x+col*scale,y+row*scale,scale,scale,ink);
}
