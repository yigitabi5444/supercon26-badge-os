// Actual app_manager.c, with explicit loader/host-lifecycle doubles, never ELF execution.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_elf.h"
#include "badge_native_api.h"
#include "controller.h"
#include "native_elf.h"
#include "native_host.h"
#include "native_video.h"
#include "native_codex.h"
#include "native_hackaday.h"
#include "native_messages.h"
#include "native_doom.h"
#include "badge/badge.h"
#include "manager_ui.h"

static const app_t home_app={.name="Codex Board"};
const app_t *const apps[]={&home_app};
const int napps=1;
extern const app_t app_app_manager;

static _Alignas(4) const uint8_t snapshot[64]={0x7f,'E','L','F'};
static const am_metadata_t metadata={.id="aliens",.version="1.0.0",.abi=2,.size=sizeof snapshot};
static am_result_t preflight_result,start_result,stop_result;
static int init_result,relocate_result,request_result;
static bool host_live,ordinary_exit;
static unsigned checks,inits,relocations,requests,deinits,sets,resets,starts,stops,observations;
static uintptr_t (*resolver)(const char *);
static esp_elf_t *retained;
static char events[128];
static size_t event_count;

static void event(char code) { assert(event_count+1<sizeof events);events[event_count++]=code;events[event_count]=0; }
static void reset(void)
{
    assert(!resolver && !host_live && !retained);
    preflight_result=start_result=stop_result=AM_OK;
    init_result=relocate_result=request_result=0;ordinary_exit=true;
    checks=inits=relocations=requests=deinits=sets=resets=starts=stops=observations=0;
    event_count=0;events[0]=0;
}
static am_result_t launch(void) { return app_manager_launch_snapshot(snapshot,sizeof snapshot,&metadata); }
static void clean_counts(unsigned init,unsigned reloc,unsigned request,unsigned deinit,unsigned start)
{
    assert(inits==init && relocations==reloc && requests==request && deinits==deinit && starts==start);
    assert(sets==1 && resets==1 && stops==1 && !resolver && !retained && !host_live);
}
am_result_t am_native_elf_check(am_bytes_t bytes,const char *id,const char *version,uint32_t abi)
{
    event('V');checks++;assert(!resolver && !retained && !host_live);
    assert(bytes.data==snapshot && bytes.size==sizeof snapshot);
    assert(!strcmp(id,"aliens") && !strcmp(version,"1.0.0") && abi==2);return preflight_result;
}
void elf_set_symbol_resolver(uintptr_t (*callback)(const char *))
{
    event('R');assert(!resolver && !retained);resolver=callback;sets++;
    assert(resolver("badge_app_get_api")==((uintptr_t)badge_app_get_api));
    const char *rejected[]={"malloc","free","esp_random","esp_restart","xTaskCreate","ui_frame",
                            "text","badge_app_get_api_extra","",NULL};
    for (unsigned i=0;i<sizeof rejected/sizeof *rejected;i++) assert(!resolver(rejected[i]));
}
void elf_reset_symbol_resolver(void)
{ event('X');assert(resolver && !retained && !host_live);resolver=NULL;resets++; }
int esp_elf_init(esp_elf_t *elf)
{
    event('I');inits++;assert(resolver && elf && !elf->initialized && !retained && !host_live);
    if (!init_result) { elf->initialized=1;retained=elf; }return init_result;
}
int esp_elf_relocate(esp_elf_t *elf,const uint8_t *bytes,size_t size)
{
    event('L');relocations++;assert(elf==retained && elf->initialized && resolver && !host_live);
    assert(bytes==snapshot && size==sizeof snapshot && !((uintptr_t)bytes&3u));return relocate_result;
}
int esp_elf_request(esp_elf_t *elf,int option,int argc,char **argv)
{
    event('Q');requests++;assert(elf==retained && elf->initialized && resolver && host_live);
    assert(!option && !argc && !argv);assert(resolver("badge_app_get_api"));return request_result;
}
void esp_elf_deinit(esp_elf_t *elf)
{
    event('D');deinits++;assert(elf==retained && elf->initialized && resolver && !host_live);
    elf->initialized=0;retained=NULL;
}
am_result_t am_native_host_start(void)
{
    event('S');starts++;assert(retained && retained->initialized && resolver && !host_live);
    if (start_result==AM_OK) host_live=true;return start_result;
}
am_result_t am_native_host_start_for(const char *id)
{ assert(!strcmp(id,"aliens") || !strcmp(id,"rocks"));return am_native_host_start(); }
am_result_t am_native_host_start_video(const am_native_video_t *v)
{(void)v;assert(!"Video path not exercised by legacy launcher fixture");return AM_DISABLED;}
am_result_t am_native_host_start_codex(const am_native_codex_t *v)
{(void)v;assert(!"Codex path not exercised by legacy launcher fixture");return AM_DISABLED;}
am_result_t am_native_host_start_hackaday(const am_native_hackaday_t *v)
{(void)v;assert(!"Hackaday path not exercised by legacy launcher fixture");return AM_DISABLED;}
am_result_t am_native_host_start_messages(const am_native_messages_t *v)
{(void)v;assert(!"Messages path not exercised by legacy launcher fixture");return AM_DISABLED;}
am_result_t am_native_host_stop(void)
{ event('T');stops++;if (stop_result==AM_OK) host_live=false;return stop_result; }
bool am_native_host_live(void) { return host_live; }
bool am_native_host_good_return(void)
{ event('G');observations++;assert(requests && host_live);return ordinary_exit; }
const badge_native_api_t *badge_app_get_api(uint32_t abi,uint32_t size)
{ (void)abi;(void)size;return NULL; } // Resolver address only; actual host API tested separately.
void app_manager_ui(void) { assert(am_has_home(0) && !am_has_home(1)); }
void icon_base(int x,int y) { (void)x;(void)y; }
void box(int x,int y,int w,int h,bool ink) { (void)x;(void)y;(void)w;(void)h;(void)ink; }
void text(int x,int y,int scale,const char *value,bool ink)
{ (void)x;(void)y;(void)scale;(void)value;(void)ink; }

static void reject_before_loader(void)
{
    reset();am_metadata_t wrong=metadata;strcpy(wrong.id,"counter");
    assert(app_manager_launch_snapshot(NULL,sizeof snapshot,&metadata)==AM_IO);
    assert(app_manager_launch_snapshot(snapshot+1,sizeof snapshot,&metadata)==AM_IO);
    assert(app_manager_launch_snapshot(snapshot,sizeof snapshot,NULL)==AM_IO);
    assert(app_manager_launch_snapshot(snapshot,sizeof snapshot-1,&metadata)==AM_IO);
    assert(app_manager_launch_snapshot(snapshot,sizeof snapshot,&wrong)==AM_IO);
    assert(!checks && !sets && !inits && !stops);
    host_live=true;assert(launch()==AM_BUSY);assert(!checks && !sets && !stops);
    assert(app_manager_quiesce_snapshot()==AM_OK);assert(!host_live);
    reset();preflight_result=AM_BAD_RECORD;assert(launch()==AM_BAD_RECORD);
    assert(checks==1 && !strcmp(events,"V") && !sets && !inits && !stops);
}
static void failure_cleanup(void)
{
    reset();init_result=-11;assert(launch()==AM_IO);clean_counts(1,0,0,0,0);
    assert(!strcmp(events,"VRITX") && !observations);
    reset();relocate_result=-12;assert(launch()==AM_IO);clean_counts(1,1,0,1,0);
    assert(!strcmp(events,"VRILTDX") && !observations);
    reset();start_result=AM_FULL;assert(launch()==AM_FULL);clean_counts(1,1,0,1,1);
    assert(!strcmp(events,"VRILSTDX") && !observations);
    reset();request_result=-13;assert(launch()==AM_IO);clean_counts(1,1,1,1,1);
    assert(!strcmp(events,"VRILSQTDX") && !observations);
    reset();ordinary_exit=false;assert(launch()==AM_IO);clean_counts(1,1,1,1,1);
    assert(!strcmp(events,"VRILSQGTDX") && observations==1);
}
static void ordinary_return_and_retry(void)
{
    reset();assert(launch()==AM_OK);clean_counts(1,1,1,1,1);
    assert(checks==1 && observations==1 && !strcmp(events,"VRILSQGTDX"));
    assert(app_manager_quiesce_snapshot()==AM_OK);
    assert(deinits==1 && resets==1 && stops==2); // Repeat cleanup never deinitializes twice.
    reset();stop_result=AM_BUSY;assert(launch()==AM_BUSY);
    assert(!strcmp(events,"VRILSQGT") && host_live && resolver && retained && retained->initialized);
    assert(checks==1 && starts==1 && requests==1 && observations==1 && !deinits && !resets);
    size_t count=event_count;assert(launch()==AM_BUSY && event_count==count);
    assert(app_manager_quiesce_snapshot()==AM_BUSY && host_live && resolver && retained);
    assert(!deinits && !resets && stops==2);
    stop_result=AM_OK;assert(app_manager_quiesce_snapshot()==AM_OK);
    assert(!strcmp(events,"VRILSQGTTTDX") && !host_live && !resolver && !retained);
    assert(deinits==1 && resets==1 && stops==3);
    assert(app_manager_quiesce_snapshot()==AM_OK && deinits==1 && resets==1);
    // The same launcher can bind a fresh module only after retained cleanup finishes.
    reset();assert(launch()==AM_OK);clean_counts(1,1,1,1,1);
}
int main(void)
{
    app_app_manager.run(); // Selected registry reaches the real catalogue before UI entry.
    reject_before_loader();failure_cleanup();ordinary_return_and_retry();
    puts("PASS: actual native launcher preflight ordering, sole API resolver, init/relocate/start/request failure cleanup, ordinary-exit evidence, retained ELF/resolver on busy stop, exact retry cleanup and re-entry; no real ELF execution.");
    return 0;
}

am_result_t am_native_host_start_doom(const am_native_doom_t *d) {(void)d;assert(0);return AM_DISABLED;}
int esp_elf_relocate_source(esp_elf_t *e,const elf_source_t *s) {(void)e;(void)s;assert(0);return -1;}

size_t am_home_apps_inventory(const app_t **out,size_t capacity)
{(void)out;(void)capacity;assert(!"Home provider is outside this sealed app fixture");return 0;}
