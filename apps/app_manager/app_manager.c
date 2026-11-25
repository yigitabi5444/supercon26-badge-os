// Verified private snapshot and store lease outlive the native loader and drawer.
#include <string.h>
#include "esp_elf.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "badge/badge.h"
#include "badge_native_api.h"
#include "controller.h"
#include "native_elf.h"
#include "native_host.h"
#include "native_video.h"
#include "native_codex.h"
#include "native_hackaday.h"
#include "native_messages.h"
#include "native_doom.h"
#include "manager_ui.h"
#include "managed_apps.h"
#include "home_apps.h"

#if !CONFIG_ELF_LOADER || !CONFIG_ELF_LOADER_LOAD_PSRAM
#error "Apply apps/app_manager/sdkconfig.defaults"
#endif
#if CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT || CONFIG_ELF_LOADER_LIBC_SYMBOLS || \
    CONFIG_ELF_LOADER_ESPIDF_SYMBOLS || CONFIG_ELF_LOADER_CUSTOMER_SYMBOLS
#error "Native apps require the sole explicit API resolver"
#endif

static esp_elf_t loaded;
static bool initialized,resolver_active;
static uintptr_t resolve_only_api(const char *name)
{
    return name && !strcmp(name,"badge_app_get_api")?(uintptr_t)badge_app_get_api:0;
}
am_result_t app_manager_quiesce_snapshot(void)
{
    am_result_t stopped=am_native_host_stop();
    if(stopped!=AM_OK)return stopped; // Retain ELF/resolver/copy/lease while drawer may run.
    if(initialized){esp_elf_deinit(&loaded);initialized=false;}
    if(resolver_active){elf_reset_symbol_resolver();resolver_active=false;}
    return AM_OK;
}
static am_result_t launch_snapshot(const uint8_t *copy,size_t size,const am_metadata_t *meta,
                                   const am_native_video_t *video,const am_native_codex_t *codex,
                                   const am_native_hackaday_t *had,const am_native_messages_t *messages)
{
    if(initialized || resolver_active || am_native_host_live())return AM_BUSY;
    if(!copy || ((uintptr_t)copy&3u) || !meta || size!=meta->size || !am_managed_id(meta->id))return AM_IO;
    if(!strcmp(meta->id,"doom"))return AM_DISABLED;
    if((!strcmp(meta->id,"video"))!=(video!=NULL))return AM_DISABLED;
    if((!strcmp(meta->id,"codex_board"))!=(codex!=NULL))return AM_DISABLED;
    if((!strcmp(meta->id,"hackaday"))!=(had!=NULL))return AM_DISABLED;
    if((!strcmp(meta->id,"messaging"))!=(messages!=NULL))return AM_DISABLED;
    am_result_t result=am_native_elf_check((am_bytes_t){copy,size},meta->id,meta->version,meta->abi);
    if(result!=AM_OK)return result;
    memset(&loaded,0,sizeof loaded);
    elf_set_symbol_resolver(resolve_only_api);resolver_active=true;
    int status=esp_elf_init(&loaded);
    if(!status) {
        initialized=true;
        status=esp_elf_relocate(&loaded,copy,size);
        if(!status) {
            result=video?am_native_host_start_video(video):codex?am_native_host_start_codex(codex):
                had?am_native_host_start_hackaday(had):messages?am_native_host_start_messages(messages):am_native_host_start_for(meta->id);
            if(result==AM_OK) {
                status=esp_elf_request(&loaded,0,0,NULL);
                // The pinned loader discards entry's return value. Require ordinary exit evidence.
                result=!status && am_native_host_good_return()?AM_OK:AM_IO;
            }
        } else result=AM_IO;
    } else result=AM_IO;
    am_result_t closed=app_manager_quiesce_snapshot();
    ESP_LOGI("app_manager","native returned %d; cleanup %d",result,closed);
    return closed==AM_OK?result:closed;
}
am_result_t app_manager_launch_snapshot(const uint8_t *copy,size_t size,const am_metadata_t *meta)
{ return launch_snapshot(copy,size,meta,NULL,NULL,NULL,NULL); }
am_result_t app_manager_launch_video_snapshot(const uint8_t *copy,size_t size,const am_metadata_t *meta,const am_native_video_t *video)
{ return launch_snapshot(copy,size,meta,video,NULL,NULL,NULL); }
am_result_t app_manager_launch_codex_snapshot(const uint8_t *copy,size_t size,const am_metadata_t *meta,const am_native_codex_t *codex)
{ return launch_snapshot(copy,size,meta,NULL,codex,NULL,NULL); }
am_result_t app_manager_launch_hackaday_snapshot(const uint8_t *copy,size_t size,const am_metadata_t *meta,const am_native_hackaday_t *had)
{ return launch_snapshot(copy,size,meta,NULL,NULL,had,NULL); }
am_result_t app_manager_launch_messages_snapshot(const uint8_t *copy,size_t size,const am_metadata_t *meta,const am_native_messages_t *messages)
{ return launch_snapshot(copy,size,meta,NULL,NULL,NULL,messages); }
typedef struct { am_store_t *store;const am_lease_t *lease;uint32_t generation,size;const char *path; } doom_source_t;
static size_t doom_size(void *ctx) { return ((doom_source_t *)ctx)->size; }
static int doom_read(void *ctx,size_t off,void *out,size_t bytes)
{
    doom_source_t *d=ctx;am_store_t *s=d->store;const am_lease_t *l=d->lease;
    if(s->lease!=l || l->owner!=s || l->generation!=d->generation || s->generation!=d->generation ||
       off>d->size || bytes>d->size-off)return -1;
    uint8_t *p=out;
    while(bytes) {
        size_t n=bytes>4096?4096:bytes,got=0;
        if(s->io.read_at(s->io.context,d->path,(uint32_t)off,p,n,&got)!=AM_OK || got!=n)return -1;
        bytes-=n;p+=n;off+=n;
    }
    return 0;
}
am_result_t app_manager_launch_doom_source(am_store_t *s,const am_lease_t *lease,const am_metadata_t *meta,
                                            const char *path,const am_native_doom_t *doom)
{
    if(initialized || resolver_active || am_native_host_live())return AM_BUSY;
    if(!s || !s->app_id || strcmp(s->app_id,"doom") || !meta || strcmp(meta->id,"doom") ||
       !lease || s->lease!=lease || lease->owner!=s || lease->generation!=s->generation || !s->io.read_at ||
       !path || !doom || doom->store!=s || doom->lease!=lease || meta->size<52 || meta->size>AM_DOOM_PAYLOAD_MAX)return AM_DISABLED;
    // am_store_doom_open authenticated, hashed and preflighted this immutable file under this same lease.
    doom_source_t context={s,lease,lease->generation,meta->size,path};
    const elf_source_t source={.size=doom_size,.read=doom_read,.ctx=&context};
    memset(&loaded,0,sizeof loaded);elf_set_symbol_resolver(resolve_only_api);resolver_active=true;
    am_result_t result=AM_IO;
    if(!esp_elf_init(&loaded)) {
        initialized=true;
        if(!esp_elf_relocate_source(&loaded,&source)) {
            result=am_native_host_start_doom(doom);
            if(result==AM_OK)result=!esp_elf_request(&loaded,0,0,NULL) && am_native_host_good_return()?AM_OK:AM_IO;
        }
    }
    am_result_t closed=app_manager_quiesce_snapshot();return closed==AM_OK?result:closed;
}
// This object is pulled by app_app_manager; a weak Home reference alone would
// not pull a separate provider object out of the component's static archive.
size_t am_home_apps_inventory(const app_t **,size_t);
size_t app_manager_home_apps(const app_t **out,size_t capacity)
{return am_home_apps_inventory(out,capacity);}
static void icon(int x,int y)
{
    icon_base(x,y);
    box(x+38,y+38,100,100,false);box(x+44,y+44,88,88,true);
    text(x+56,y+56,8,"+",false);
}
// Use the selected Home registry, including stock builds without optional apps.
extern const app_t *const apps[];
extern const int napps;
static void run(void)
{
    am_home_reset();
    for(int i=0;i<napps;i++)if(apps[i])am_home_add(apps[i]->name);
    app_manager_ui();
}
const app_t app_app_manager={.name="App Manager",.icon=icon,.run=run};
#if CONFIG_BADGE_APP_MANAGER_HELP
const app_info_t app_app_manager_info={
    .info="Left: Actions and Next app. Right: scroll details. Hold both MENU buttons for Home.",
    .credit="Espressif elf_loader 2.1.0 (Apache-2.0); Badge OS native app adapter.",
    .launch_info=true,
};
#endif
