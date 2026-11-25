// Focused actual-source frame-broker checks; no firmware build, ELF execution or hardware.
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "badge_native_api.h"
#include "native_host.h"
#include "native_video.h"
#include "native_host_double.h"
#include "badge/badge.h"
am_result_t am_video_asset_read(const am_video_asset_t *asset,uint32_t offset,uint8_t *out,size_t n)
{ (void)asset;(void)offset;(void)out;(void)n;assert(!"Video is not enrolled in legacy host checks");return AM_DISABLED; }
bool am_video_asset_live(const am_video_asset_t *asset)
{ (void)asset;assert(!"Video is not enrolled in legacy host checks");return false; }

static const badge_native_api_t *start(void)
{
    nh_reset();assert(!am_native_host_live());
    assert(!badge_app_get_api(BADGE_NATIVE_ABI,sizeof(badge_native_api_t)));
    assert(am_native_host_start()==AM_OK);assert(am_native_host_live());
    assert(!am_native_host_good_return());assert(am_native_host_start()==AM_BUSY);
    const badge_native_api_t *api=badge_app_get_api(BADGE_NATIVE_ABI,sizeof *api);assert(api);
    assert(api->abi_version==2 && api->struct_size==sizeof *api && api->context);
    assert(api->width==W && api->height==H && api->top==TOP);
    assert(nh_flushed==1 && !strcmp(nh_labels[0],"FIRE") && !strcmp(nh_labels[1],"FIRE"));
    assert(!strcmp(nh_labels[2],"LEFT") && !strcmp(nh_labels[3],"RIGHT"));
    return api;
}
static void stopped(void)
{
    assert(am_native_host_stop()==AM_OK);assert(!am_native_host_live());
    assert(atomic_load(&nh_sem_created)==atomic_load(&nh_sem_deleted));
    assert(atomic_load(&nh_tasks_created)==atomic_load(&nh_tasks_deleted));
    assert(am_native_host_stop()==AM_OK);
}
static void allocation_failures(void)
{
    for (unsigned i=1;i<=2;i++) {
        nh_reset();nh_fail_sem=i;assert(am_native_host_start()==AM_FULL);stopped();
    }
    nh_reset();nh_fail_task=1;assert(am_native_host_start()==AM_FULL);stopped();
}
static void normal_frames(void)
{
    const badge_native_api_t *api=start();void *ctx=api->context;
    badge_native_input_t input;nh_memory(&input,sizeof input,NH_DRAM,true);
    char stack_text[]="SCORE 42",psram_text[]="WAVE 1",max_text[65];
    static const char flash_text[]="GAME OVER";
    memset(max_text,'A',64);max_text[64]=0;
    nh_memory(stack_text,sizeof stack_text,NH_DRAM,true);nh_memory(psram_text,sizeof psram_text,NH_PSRAM,true);
    nh_memory(flash_text,sizeof flash_text,NH_DROM,false);nh_memory(max_text,sizeof max_text,NH_DRAM,true);
    memset(fb,0xa5,W*TOP/8);atomic_store(&nh_notify_paused,true);atomic_store(&nh_panel_blocked,true);
    assert(api->begin_frame(ctx));api->clear(ctx);
    api->box(ctx,-10,TOP-10,20,20,1);assert(fb[TOP*(W/8)]==0xff);
    api->box(ctx,INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX,1);
    api->box(ctx,INT32_MIN,INT32_MIN,INT32_MAX,INT32_MAX,1);
    api->outline(ctx,-5,TOP-5,20,20,INT32_MAX,1);
    api->outline(ctx,INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX,0);
    for (int scale=1;scale<=4;scale++) api->text(ctx,-4,TOP-4,scale,stack_text,1);
    api->text(ctx,20,TOP+40,2,psram_text,1);api->text(ctx,20,TOP+80,3,flash_text,1);
    api->text(ctx,INT32_MAX,INT32_MAX,4,max_text,1);
    api->text(ctx,INT32_MIN,INT32_MIN,4,max_text,1);
    for (unsigned i=0;i<W*TOP/8;i++) assert(fb[i]==0xa5); // No module draw touches the OS bar.
    unsigned drawn=atomic_load(&nh_boxes);api->submit_frame(ctx);
    // A queued frame excludes the next writer even before the drawer acquires the mutex.
    assert(!api->begin_frame(ctx));assert(!atomic_load(&nh_frames_started));
    atomic_store(&nh_notify_paused,false);nh_await(&nh_frames_started,1);
    assert(!api->begin_frame(ctx));assert(atomic_load(&nh_boxes)==drawn);
    atomic_store(&nh_held,0x80000005u);nh_knob=4095;
    nh_press(BTN_PLUS);nh_press(BTN_MINUS);nh_press(BTN_A);nh_press(BTN_B);nh_press(7);
    api->input(ctx,&input);assert(input.held==5 && input.pressed==15 && input.knob==4095 && !input.exit_requested);
    assert(api->random(ctx)==0x12345678);uint32_t before=api->now_ms(ctx);
    api->wait_ms(ctx,50);assert((uint32_t)(api->now_ms(ctx)-before)>=50);
    assert(!atomic_load(&nh_frames_done)); // Tick/input callbacks did not wait for the panel.
    atomic_store(&nh_panel_blocked,false);api->frame_idle(ctx);
    assert(atomic_load(&nh_frames_done)==1 && atomic_load(&nh_closes)==1);
    assert(api->begin_frame(ctx));api->clear(ctx);api->submit_frame(ctx);api->wipe(ctx);
    assert(atomic_load(&nh_frames_done)==2 && atomic_load(&nh_wipes)==1);
    // The original 3-second game-over pause is accepted, with exit checked every20ms.
    atomic_store(&nh_held,NATIVE_A|NATIVE_B);before=api->now_ms(ctx);
    api->wait_ms(ctx,3000);assert((uint32_t)(api->now_ms(ctx)-before)==600);
    api->input(ctx,&input);assert(input.exit_requested && !input.pressed);
    atomic_store(&nh_held,0);nh_knob=4096;api->input(ctx,&input);
    assert(input.exit_requested && input.knob==-1 && am_native_host_good_return());
    stopped();assert(am_native_host_good_return());
    assert(atomic_load(&nh_frames_done)==2 && atomic_load(&nh_closes)==2);
    assert(!badge_app_get_api(BADGE_NATIVE_ABI,sizeof *api));
    assert(!api->begin_frame(ctx) && !api->now_ms(ctx));
    for (unsigned i=0;i<5;i++) nh_sleep();
    assert(atomic_load(&nh_frames_done)==2); // No activity survives successful stop.
    nh_reset();nh_fail_sem=1;assert(am_native_host_start()==AM_FULL);
    assert(!am_native_host_good_return());stopped(); // Failed starts clear prior evidence too.
}
static void exit_wrap_and_observation(void)
{
    const badge_native_api_t *api=start();void *ctx=api->context;badge_native_input_t input;
    nh_memory(&input,sizeof input,NH_PSRAM,true);
    assert(api->begin_frame(ctx));api->submit_frame(ctx);api->frame_idle(ctx);
    atomic_store(&nh_time_ms,UINT32_MAX-300u);atomic_store(&nh_held,NATIVE_A|NATIVE_B);
    api->input(ctx,&input);assert(!input.exit_requested);
    atomic_fetch_add(&nh_time_ms,599);api->input(ctx,&input);assert(!input.exit_requested);
    // Release before the threshold resets the hold duration.
    atomic_store(&nh_held,NATIVE_A);api->input(ctx,&input);
    atomic_store(&nh_held,NATIVE_A|NATIVE_B);api->input(ctx,&input);
    atomic_fetch_add(&nh_time_ms,600);api->wait_ms(ctx,0);
    assert(!am_native_host_good_return()); // Detection alone is not module observation.
    api->input(ctx,&input);assert(input.exit_requested && am_native_host_good_return());
    api->clear(ctx);assert(!am_native_host_good_return());stopped(); // Faults invalidate otherwise complete evidence.
}
static void rocks_controls(void)
{
    nh_reset();assert(am_native_host_start_for("unknown")==AM_DISABLED);
    assert(!atomic_load(&nh_sem_created));
    assert(am_native_host_start_for("rocks")==AM_OK);
    const badge_native_api_t *api=badge_app_get_api(BADGE_NATIVE_ABI,sizeof *api);assert(api);
    assert(!strcmp(nh_labels[0],"Turn L") && !strcmp(nh_labels[1],"Turn R"));
    assert(!strcmp(nh_labels[2],"Thrust") && !strcmp(nh_labels[3],"Fire"));
    badge_native_input_t input;nh_memory(&input,sizeof input,NH_DRAM,true);
    atomic_store(&nh_held,NATIVE_A|NATIVE_B);
    for(unsigned tick=0;tick<30;tick++) {
        uint32_t before=api->now_ms(api->context);
        api->input(api->context,&input);
        assert(input.held==(NATIVE_A|NATIVE_B) && !input.exit_requested);
        api->wait_ms(api->context,40);
        assert((uint32_t)(api->now_ms(api->context)-before)==40);
    }
    assert(!am_native_host_good_return()); // MENU restart is not cooperative-exit evidence.
    stopped();
}
typedef struct { const badge_native_api_t *api;uint32_t now;int32_t begin;am_result_t stop; } wrong_task_t;
static void *wrong_task(void *argument)
{
    wrong_task_t *result=argument;const badge_native_api_t *api=result->api;
    result->now=api->now_ms(api->context);result->begin=api->begin_frame(api->context);
    assert(!badge_app_get_api(BADGE_NATIVE_ABI,sizeof *api));result->stop=am_native_host_stop();return NULL;
}
static void rejected_requests(void)
{
    const badge_native_api_t *api=start();void *ctx=api->context;
    assert(!badge_app_get_api(1,sizeof *api));assert(!badge_app_get_api(2,(uint32_t)sizeof *api+1));
    assert(!api->random(NULL));api->clear(ctx);api->submit_frame(ctx);
    wrong_task_t result={.api=api};pthread_t other;assert(!pthread_create(&other,NULL,wrong_task,&result));
    assert(!pthread_join(other,NULL));assert(!result.now && !result.begin && result.stop==AM_BUSY);
    badge_native_input_t ro={99,98,97,96},partial={99,98,97,96},inaccessible={99,98,97,96};
    _Alignas(4) char unaligned[32]={0};
    nh_memory(&ro,sizeof ro,NH_DROM,true);nh_memory(&partial,sizeof partial-1,NH_DRAM,true);
    nh_memory(&inaccessible,sizeof inaccessible,NH_PSRAM,false);nh_memory(unaligned,sizeof unaligned,NH_DRAM,true);
    api->input(ctx,&ro);api->input(ctx,&partial);api->input(ctx,&inaccessible);
    api->input(ctx,(badge_native_input_t *)(void *)(unaligned+1));api->input(ctx,NULL);
    api->input(ctx,(badge_native_input_t *)(uintptr_t)(UINTPTR_MAX-3));
    assert(ro.held==99 && partial.held==99 && inaccessible.held==99 && !unaligned[1]);
    static const char irom[]="IROM",not_bytes[]="no byte access";
    char too_long[66];memset(too_long,'X',65);too_long[65]=0;
    nh_memory(irom,sizeof irom,NH_IROM,true);nh_memory(not_bytes,sizeof not_bytes,NH_DRAM,false);
    nh_memory(too_long,sizeof too_long,NH_DRAM,true);
    assert(api->begin_frame(ctx));assert(!api->begin_frame(ctx));
    unsigned drawn=atomic_load(&nh_boxes);
    api->text(ctx,0,TOP,1,irom,1);api->text(ctx,0,TOP,1,not_bytes,1);api->text(ctx,0,TOP,1,too_long,1);
    api->text(ctx,0,TOP,1,(const char *)(uintptr_t)1,1);api->text(ctx,0,TOP,1,NULL,1);
    api->text(ctx,0,TOP,0,too_long,1);api->text(ctx,0,TOP,5,too_long,1);
    assert(atomic_load(&nh_boxes)==drawn);
    api->frame_idle(ctx);api->wipe(ctx);assert(!atomic_load(&nh_closes) && !atomic_load(&nh_wipes));
    uint32_t before=api->now_ms(ctx);api->wait_ms(ctx,3001);assert(api->now_ms(ctx)==before);
    // An abandoned lease is dropped and flagged before the drawer is stopped.
    stopped();assert(!am_native_host_good_return() && !atomic_load(&nh_frames_started));
}
static void busy_frame_stop(void)
{
    const badge_native_api_t *api=start();atomic_store(&nh_panel_blocked,true);
    assert(api->begin_frame(api->context));api->submit_frame(api->context);nh_await(&nh_frames_started,1);
    assert(am_native_host_stop()==AM_BUSY && am_native_host_live());
    assert(!atomic_load(&nh_sem_deleted) && !atomic_load(&nh_tasks_deleted));
    assert(am_native_host_start()==AM_BUSY && !api->begin_frame(api->context));
    unsigned drawn=atomic_load(&nh_boxes);api->clear(api->context);assert(atomic_load(&nh_boxes)==drawn);
    atomic_store(&nh_panel_blocked,false);stopped();assert(atomic_load(&nh_frames_done)==1);
}
static void busy_park_stop(void)
{
    (void)start();atomic_store(&nh_suspend_paused,true);
    assert(am_native_host_stop()==AM_BUSY && am_native_host_live());
    assert(!atomic_load(&nh_sem_deleted) && !atomic_load(&nh_tasks_deleted));
    atomic_store(&nh_suspend_paused,false);stopped();
    (void)start();atomic_store(&nh_force_on_core,true);
    assert(am_native_host_stop()==AM_BUSY && am_native_host_live());
    assert(!atomic_load(&nh_sem_deleted) && !atomic_load(&nh_tasks_deleted));
    atomic_store(&nh_force_on_core,false);stopped();
}
int main(void)
{
    allocation_failures();normal_frames();rocks_controls();exit_wrap_and_observation();rejected_requests();
    busy_frame_stop();busy_park_stop();
    puts("PASS: actual native host ownership, queued/presenting exclusion, overflow-safe TOP clipping, stack/PSRAM/DROM pointers, input/exit timing, fast wipe drainage and retryable shutdown quiescence; platform doubles only.");
    return 0;
}
