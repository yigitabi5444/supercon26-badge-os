#include <string.h>
#include "badge/badge.h"
#include "esp_heap_caps.h"
#include "controller_target.h"
#include "txn_crypto_psa.h"
#include "txn_littlefs.h"
#include "package_http.h"
#include "managed_apps.h"
#include "native_video.h"
#include "native_codex.h"
#include "native_hackaday.h"
#include "native_messages.h"
#include "native_doom.h"

// A configured channel supplies strong public descriptors for these fixed IDs.
const am_source_config_t am_rocks_source_config __attribute__((weak))={0};
const am_source_config_t am_codex_source_config __attribute__((weak))={0};
const am_source_config_t am_hackaday_source_config __attribute__((weak))={0};
const am_source_config_t am_messages_source_config __attribute__((weak))={0};
const am_source_config_t am_doom_source_config __attribute__((weak))={0};

typedef struct {
    am_littlefs_t filesystem;am_psa_context_t crypto;
    am_layout_policy_t policy;am_source_t source;bool source_ready,trust_ready;
    const am_source_config_t *sources[AM_COUNT],*trust;
    const am_video_source_config_t *video;am_controller_t *controller;
} target_t;
static target_t target;
// Optional until the separately reviewed two-file OS companion is integrated.
#if defined(__APPLE__)
extern bool btn_home_prompt_end(void) __attribute__((weak_import));
#else
extern bool btn_home_prompt_end(void) __attribute__((weak));
#endif

static void *doom_temporary(size_t n) { return n<=AM_DOOM_PAYLOAD_MAX?heap_caps_aligned_alloc(4,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL; }
static am_result_t open_store_mode(void *context,am_store_t *store,bool (*cancel)(void *),void *owner,bool readonly)
{
    target_t *t=context;store->doom_allocate=doom_temporary;store->doom_free=heap_caps_free;
    am_result_t r=am_littlefs_init(&t->filesystem);
    if(r!=AM_OK)return r;
    if(!t->trust_ready)return AM_DISABLED;
    if(t->trust_ready) {
        const am_source_config_t *c=t->trust;
        r=am_psa_adapter(&t->crypto,c->public_key,c->key_size,&store->crypto);
        if(r!=AM_OK)return AM_DISABLED;
    }
    return readonly?am_littlefs_open_readonly(&t->filesystem,&t->policy,store):
        am_littlefs_open(&t->filesystem,&t->policy,store,cancel,owner);
}
static am_result_t open_store(void *ctx,am_store_t *s,bool (*cancel)(void *),void *owner)
{return open_store_mode(ctx,s,cancel,owner,false);}
static am_result_t close_store(void *context)
{ return am_littlefs_close(&((target_t *)context)->filesystem); }
static am_result_t incoming(void *context,am_package_t *package)
{ return am_source_cached(&((target_t *)context)->source,package); }
static void discard(void *context)
{
    target_t *t=context;t->source.valid=false;memset(&t->source.package,0,sizeof t->source.package);
    heap_caps_free(t->source.bytes);t->source.bytes=NULL;
    heap_caps_free(t->source.doom_payload);t->source.doom_payload=NULL;
}
static am_result_t select_app(void *context,am_store_t *store,const char *id,bool create,bool *ready)
{
    target_t *t=context;unsigned selected=0;
    if(!am_managed_id(id))return AM_DISABLED;
    for(;selected<AM_COUNT;selected++)if(!strcmp(am_catalogue[selected].id,id))break;
    if(selected==AM_COUNT)return AM_DISABLED;
    const am_source_config_t *config=t->sources[selected];
    bool available=am_https_enabled() && am_source_config_valid(config) && t->trust_ready &&
        !memcmp(config->public_key,t->trust->public_key,65);
    if(!strcmp(id,"video"))available=available && am_video_source_valid(t->video);
    *ready=available;
    if(t->filesystem.mounted) {
        am_result_t r=am_littlefs_select(&t->filesystem,store,id,create && available);
        if(r!=AM_OK)return r;
    }
    heap_caps_free(t->source.doom_payload);t->source.doom_payload=NULL;
    // One shared cache; old spans can never be published under a different app ID.
    t->source.valid=false;memset(&t->source.package,0,sizeof t->source.package);
    t->source.config=config;t->source.app_id=id;t->source_ready=available;*ready=available;
    return AM_OK;
}
static am_result_t fetch(void *context,bool (*cancel)(void *),void *owner)
{
    target_t *t=context;
    if(!t->source_ready)return AM_DISABLED;
    t->source.valid=false;memset(&t->source.package,0,sizeof t->source.package);
    if(!t->source.bytes)t->source.bytes=heap_caps_aligned_alloc(4,sizeof *t->source.bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!t->source.bytes)return AM_IO;
    bool doom=t->source.app_id && !strcmp(t->source.app_id,"doom");
    if(doom && !t->source.doom_payload)t->source.doom_payload=doom_temporary(AM_DOOM_PAYLOAD_MAX);
    if(doom && !t->source.doom_payload){discard(t);return AM_IO;}
    am_https_t *http=heap_caps_aligned_alloc(4,sizeof *http,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!http){discard(t);return AM_IO;}
    am_crypto_t crypto;am_result_t r=am_psa_adapter(&t->crypto,t->source.config->public_key,
        t->source.config->key_size,&crypto);
    memset(http,0,sizeof *http);
    if(r==AM_OK)r=am_https_begin(http,t->source.config,cancel,owner,t->source.progress);
    if(r==AM_OK) {
        const am_runtime_t runtime={"esp32s3","6.0.2","2.1.0",2};
        r=am_source_fetch(&t->source,&crypto,&runtime,doom?am_https_doom_get:am_https_get,http,cancel,owner);
    }
    am_https_end(http);heap_caps_free(http);
    if(r!=AM_OK)discard(t);
    return r==AM_IO?AM_DOWNLOAD_FAILED:r;
}
am_result_t am_target_prepare(am_controller_t *controller,am_progress_t progress)
{
    target.source.progress=progress;
    if(!controller || !controller->platform.source_ready || controller->busy || controller->lease.owner ||
       (controller->state!=AM_READY && controller->state!=AM_UNASSIGNED))return AM_DISABLED;
    am_result_t result=am_https_prepare(target.source.config,am_controller_cancel,controller,progress);
    if(am_controller_cancel(controller))controller->exit_requested=true;
    return result;
}
static void phase(void *context,const char *message)
{
    target_t *t=context;
    if(t->source.progress)t->source.progress(message);
}
static void *allocate(void *context,size_t size,bool execution)
{
    (void)context;
    (void)execution;
    unsigned caps=MALLOC_CAP_8BIT|MALLOC_CAP_SPIRAM;
    return heap_caps_aligned_alloc(4,size,caps);
}
static void release(void *context,void *memory) { (void)context;heap_caps_free(memory); }
static void home_mode(void *context,bool enabled) { (void)context;btn_home_prompt(enabled); }
static bool home_request(void *context) { (void)context;return btn_home_requested(); }
static bool home_end(void *context) { (void)context;return btn_home_prompt_end(); }
static am_result_t launch(void *context,const uint8_t *copy,size_t size,const am_metadata_t *meta)
{
    target_t *t=context;
    if(!strcmp(meta->id,"messaging"))return app_manager_launch_messages_snapshot(copy,size,meta,&am_native_messages_target);
    if(!strcmp(meta->id,"hackaday"))return app_manager_launch_hackaday_snapshot(copy,size,meta,&am_native_hackaday_target);
    if(!strcmp(meta->id,"codex_board"))return app_manager_launch_codex_snapshot(copy,size,meta,&am_native_codex_target);
    if(strcmp(meta->id,"video"))return app_manager_launch_snapshot(copy,size,meta);
    am_video_store_t store={&t->controller->store,am_psa_sha256_stream};
    am_native_video_t video={.inflate=am_native_video_inflate,.info=am_native_video_info};
    am_result_t r=am_video_asset_open(&store,&t->controller->lease,AM_VIDEO_CLIP_HASH,&video.asset);
    return r==AM_OK?app_manager_launch_video_snapshot(copy,size,meta,&video):r;
}
static am_result_t prepare(void *context,am_store_t *s,bool (*cancel)(void *),void *owner)
{
    if(strcmp(s->app_id,"video"))return AM_OK;
    target_t *t=context;am_video_store_t store={s,am_psa_sha256_stream};
    am_https_t *http=heap_caps_aligned_alloc(4,sizeof *http,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!http)return AM_IO;
    am_result_t r=am_video_source_prepare(&store,t->video,http,cancel,owner,t->source.progress);
    heap_caps_free(http);
    if(r!=AM_OK && r!=AM_UP_TO_DATE)discard(t); // Incomplete asset must not appear as a ready package.
    return r;
}
static am_result_t launch_doom(void *ctx,am_store_t *s,am_lease_t *l,const am_metadata_t *m,const char *path)
{ (void)ctx;return am_native_doom_target_launch(s,l,m,path); }
static am_result_t quiesce(void *context) { (void)context;return app_manager_quiesce_snapshot(); }
am_result_t am_target_start_with_profile(am_controller_t *controller,const am_source_config_t *config,
                                         const am_layout_policy_t *policy)
{ return am_target_start_with_sources(controller,config,NULL,policy); }
am_result_t am_target_start_with_sources(am_controller_t *controller,const am_source_config_t *config,
                                        const am_source_config_t *rocks,const am_layout_policy_t *policy)
{ return am_target_start_with_catalogue(controller,config,rocks,NULL,policy); }
am_result_t am_target_start_with_catalogue(am_controller_t *controller,const am_source_config_t *config,
                                          const am_source_config_t *rocks,const am_video_source_config_t *video,
                                          const am_layout_policy_t *policy)
{ return am_target_start_with_apps(controller,config,rocks,video,NULL,policy); }
am_result_t am_target_start_with_apps(am_controller_t *controller,const am_source_config_t *config,
                                     const am_source_config_t *rocks,const am_video_source_config_t *video,
                                     const am_source_config_t *codex,const am_layout_policy_t *policy)
{ return am_target_start_with_downloads(controller,config,rocks,video,codex,NULL,policy); }
am_result_t am_target_start_with_downloads(am_controller_t *controller,const am_source_config_t *config,
                                          const am_source_config_t *rocks,const am_video_source_config_t *video,
                                          const am_source_config_t *codex,const am_source_config_t *hackaday,
                                          const am_layout_policy_t *policy)
{ return am_target_start_with_packages(controller,config,rocks,video,codex,hackaday,NULL,policy); }
static am_result_t setup_target(am_controller_t *controller,const am_source_config_t *config,
    const am_source_config_t *rocks,const am_video_source_config_t *video,const am_source_config_t *codex,
    const am_source_config_t *hackaday,const am_source_config_t *messages,const am_layout_policy_t *policy,am_platform_t *platform)
{
    if(target.source.bytes || target.filesystem.mounted)return AM_BUSY; // Do not overwrite a possibly live previous context/cache.
    target.policy=policy?*policy:(am_layout_policy_t){0};
    target.video=video;target.controller=controller;
    memset(target.sources,0,sizeof target.sources);
    for(unsigned i=0;i<AM_COUNT;i++) {
        if(!strcmp(am_catalogue[i].id,"aliens"))target.sources[i]=config;
        if(!strcmp(am_catalogue[i].id,"rocks"))target.sources[i]=rocks;
        if(!strcmp(am_catalogue[i].id,"video"))target.sources[i]=video?video->code:NULL;
        if(!strcmp(am_catalogue[i].id,"codex_board"))target.sources[i]=codex;
        if(!strcmp(am_catalogue[i].id,"hackaday"))target.sources[i]=hackaday;
        if(!strcmp(am_catalogue[i].id,"messaging"))target.sources[i]=messages;
        if(!strcmp(am_catalogue[i].id,"doom"))target.sources[i]=&am_doom_source_config;
    }
    target.trust=config && config->public_key?config:rocks && rocks->public_key?rocks:
        video && video->code && video->code->public_key?video->code:codex && codex->public_key?codex:
        hackaday && hackaday->public_key?hackaday:messages && messages->public_key?messages:&am_doom_source_config;
    const am_source_config_t *trust=target.trust;
    target.source=(am_source_t){.config=config};target.source_ready=am_https_enabled() && am_source_config_valid(config);
    target.trust_ready=trust && trust->public_key && trust->key_size==65 && trust->public_key[0]==4;
    *platform=(am_platform_t){.context=&target,.assigned=target.policy.qualified && target.trust_ready,
        .open=open_store,.close=close_store,.incoming=incoming,.allocate=allocate,.free=release,
        .source_ready=target.source_ready,.fetch=fetch,.discard=discard,
        .home_mode=home_mode,.home_request=home_request,.home_end=btn_home_prompt_end?home_end:NULL,
        .launch=launch,.quiesce=quiesce,.progress=phase,.select=select_app,.prepare=prepare,.launch_doom=launch_doom};
    return AM_OK;
}
am_result_t am_target_start_with_packages(am_controller_t *controller,const am_source_config_t *config,
    const am_source_config_t *rocks,const am_video_source_config_t *video,const am_source_config_t *codex,
    const am_source_config_t *hackaday,const am_source_config_t *messages,const am_layout_policy_t *policy)
{
    if(!controller)return AM_IO;
    am_platform_t p;am_result_t r=setup_target(controller,config,rocks,video,codex,hackaday,messages,policy,&p);
    return r==AM_OK?am_controller_init(controller,p,(am_runtime_t){"esp32s3","6.0.2","2.1.0",2}):r;
}
static am_result_t default_target(am_controller_t *c,am_platform_t *p)
{return setup_target(c,&am_app_source_config,&am_rocks_source_config,&am_video_source_config,
    &am_codex_source_config,&am_hackaday_source_config,&am_messages_source_config,&am_app_layout_policy,p);}
am_result_t am_target_start_with_source(am_controller_t *controller,const am_source_config_t *config)
{ return am_target_start_with_profile(controller,config,NULL); }
am_result_t am_target_start(am_controller_t *controller)
{
    if(!controller)return AM_IO;
    am_platform_t p;am_result_t r=default_target(controller,&p);
    return r==AM_OK?am_controller_init(controller,p,(am_runtime_t){"esp32s3","6.0.2","2.1.0",2}):r;
}
am_result_t am_target_home_inventory(unsigned wanted,bool installed[AM_COUNT])
{
    if(!installed)return AM_IO;
    memset(installed,0,sizeof(bool)*AM_COUNT);am_platform_t p;
    am_result_t r=default_target(NULL,&p);if(r!=AM_OK)return r;
    if(!p.assigned || !p.home_end)return AM_DISABLED;
    am_store_t s={.runtime={"esp32s3","6.0.2","2.1.0",2}};
    s.work=allocate(&target,sizeof *s.work,false);if(!s.work)return AM_IO;
    r=open_store_mode(&target,&s,NULL,NULL,true);
    if(r==AM_OK)for(unsigned id=0;id<AM_COUNT;id++)if(wanted&(1u<<id)) {
        am_result_t selected=am_littlefs_select(&target.filesystem,&s,am_catalogue[id].id,false);
        if(selected==AM_NOT_FOUND)continue;
        if(selected!=AM_OK){r=selected;break;}
        am_metadata_t meta;am_result_t status=am_store_lookup(&s,&meta);
        if(status==AM_IO || status==AM_BUSY){r=status;break;}
        installed[id]=status==AM_OK;
    }
    am_result_t closed=close_store(&target);if(closed!=AM_OK)r=closed;
    release(&target,s.work);
    if(r!=AM_OK)memset(installed,0,sizeof(bool)*AM_COUNT);
    return r;
}
