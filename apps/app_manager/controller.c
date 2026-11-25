#include <string.h>
#include "controller.h"
#include "managed_apps.h"

#define P(c,fn,...) ((c)->platform.fn((c)->platform.context, __VA_ARGS__))
#define P0(c,fn) ((c)->platform.fn((c)->platform.context))
bool am_controller_cancel(void *context)
{
    am_controller_t *c=context;
    if(c->platform.home_request && P0(c,home_request))c->home_pending=true;
    return c->home_pending;
}
static void mode(am_controller_t *c,bool enabled)
{
    if(enabled && !c->deferred && c->platform.home_end && c->platform.home_mode) {
        P(c,home_mode,true);c->deferred=true;
    } else if(!enabled && c->deferred) {
        if(P0(c,home_end))c->home_pending=true;
        c->deferred=false;c->exit_requested=c->home_pending;
    }
}
static bool pending(am_controller_t *c)
{
    am_controller_cancel(c);
    c->exit_requested=c->home_pending;return c->home_pending;
}
static am_result_t begin(am_controller_t *c)
{
    if(c->busy)return AM_BUSY;
    c->busy=true;mode(c,true);return AM_OK;
}
static am_result_t finish(am_controller_t *c,am_result_t r)
{
    pending(c);mode(c,false);pending(c);c->busy=false;c->last=r;return r;
}
static am_result_t release_execution(am_controller_t *c)
{
    if(!c->execution_copy && !c->lease.owner)return AM_OK;
    if(c->platform.quiesce) {
        am_result_t r=P0(c,quiesce);if(r!=AM_OK)return r;
    }
    if(c->lease.owner) {
        am_result_t r=am_store_release(&c->store,&c->lease);if(r!=AM_OK)return r;
    }
    if(c->execution_copy)P(c,free,c->execution_copy);
    c->execution_copy=NULL;return AM_OK;
}
static unsigned native_index(void)
{
    for(unsigned i=0;i<AM_COUNT;i++)if(!strcmp(am_catalogue[i].id,"aliens"))return i;
    return AM_COUNT;
}
static am_result_t select_app(am_controller_t *c,unsigned selected,bool create)
{
    if(selected>=AM_COUNT || !am_managed_id(am_catalogue[selected].id))return AM_DISABLED;
    if(c->lease.owner || c->execution_copy)return AM_BUSY;
    if(selected==c->selected)return AM_OK;
    bool ready=c->platform.source_ready;
    am_result_t r=c->platform.select?P(c,select,&c->store,am_catalogue[selected].id,create,&ready):
        selected==native_index()?AM_OK:AM_DISABLED;
    if(r==AM_NOT_FOUND)c->local[selected].fetch_available=ready && c->platform.fetch && c->platform.home_end;
    if(r!=AM_OK)return r;
    if(c->selected<AM_COUNT) {
        am_local_t *old=&c->local[c->selected];
        old->package_available=old->update_available=false;
        old->package_version=NULL;old->package_size=0;
    }
    c->selected=selected;c->store.app_id=am_catalogue[selected].id;
    c->platform.source_ready=ready;
    c->local[selected].fetch_available=ready && c->platform.fetch && c->platform.home_end &&
        c->platform.home_mode && c->platform.home_request;
    return AM_OK;
}
static am_result_t close_store(am_controller_t *c)
{
    am_result_t quiesced=release_execution(c);
    if(quiesced!=AM_OK){c->state=AM_FAULT;return quiesced;}
    if(c->opened) {
        am_result_t r=P0(c,close);
        if(r!=AM_OK){c->state=AM_FAULT;return r;}
        c->opened=false;
    }
    if(c->platform.discard)P0(c,discard);
    if(c->store.work){P(c,free,c->store.work);c->store.work=NULL;}
    c->state=AM_CLOSED;return AM_OK;
}
static bool newer(const am_metadata_t *a,const am_metadata_t *b)
{
    for(unsigned i=0;i<3;i++)if(a->parts[i]!=b->parts[i])return a->parts[i]>b->parts[i];
    return false;
}
static am_result_t candidate(am_controller_t *c,am_package_t *package,am_metadata_t *meta)
{
    if(!c->platform.incoming)return AM_DISABLED;
    am_result_t r=P(c,incoming,package);char token[65];
    if(r==AM_OK)r=am_package_integrity(&c->store.crypto,*package,meta,token);
    if(r==AM_OK && (strcmp(meta->id,c->store.app_id) || !am_package_compatible(meta,&c->store.runtime)))r=AM_DISABLED;
    return r;
}
static void availability(am_controller_t *c,am_local_t *local,const am_metadata_t *installed)
{
    am_package_t input={0};am_metadata_t next;
    am_result_t r=candidate(c,&input,&next);
    local->fetch_available=c->platform.source_ready && c->platform.fetch && c->platform.home_end;
    local->package_available=r==AM_OK;
    local->package_version=NULL;local->package_size=0;
    if(r==AM_OK) {
        memcpy(c->package_version[c->selected],next.version,sizeof next.version);
        local->package_version=c->package_version[c->selected];local->package_size=next.size;
    }
    local->update_available=r==AM_OK && local->installed && installed && newer(&next,installed);
}
static am_result_t refresh(am_controller_t *c)
{
    am_metadata_t meta={0};am_result_t r=am_store_lookup(&c->store,&meta);
    if(r!=AM_OK && r!=AM_DISABLED && r!=AM_NOT_FOUND){c->local[c->selected].disabled=true;return r;}
    am_local_t local={0};
    if(r!=AM_NOT_FOUND) {
        memcpy(c->version[c->selected],meta.version,sizeof meta.version);
        local.installed=true;local.disabled=r==AM_DISABLED;local.version=c->version[c->selected];local.size=meta.size;
    }
    am_result_t space=c->store.io.space(c->store.io.context,&c->used,&c->capacity);
    if(space!=AM_OK || c->used>c->capacity){c->state=AM_FAULT;return space==AM_OK?AM_IO:space;}
    availability(c,&local,&meta);
    c->local[c->selected]=local;return AM_OK;
}
am_result_t am_controller_init(am_controller_t *c,am_platform_t platform,am_runtime_t runtime)
{
    if(!c || !platform.open || !platform.close || !platform.allocate || !platform.free)return AM_IO;
    if(native_index()==AM_COUNT)return AM_DISABLED;
    memset(c,0,sizeof *c);c->platform=platform;c->store.runtime=runtime;c->state=AM_UNASSIGNED;c->selected=AM_COUNT;
    begin(c);am_result_t r=select_app(c,native_index(),false);
    if(r!=AM_OK)return finish(c,r);
    if(platform.assigned && (!platform.home_end || !platform.home_mode || !platform.home_request)) {
        return finish(c,AM_DISABLED);
    }
    r=AM_OK;
    if(platform.assigned) {
        c->store.work=P(c,allocate,sizeof *c->store.work,false);
        if(!c->store.work)r=AM_IO;
    }
    if(r==AM_OK) {
        c->opened=true;r=P(c,open,&c->store,am_controller_cancel,c);
        if(r==AM_OK && !platform.assigned)r=AM_IO; // Unexpected assignment cannot enable actions.
        if(r==AM_OK){c->state=AM_READY;r=am_store_recover(&c->store);}
        if(r==AM_OK)r=refresh(c);
    }
    if(r==AM_OK && c->state==AM_READY && platform.select) {
        unsigned first=c->selected;
        for(unsigned i=0;i<AM_COUNT;i++)if(i!=first && am_managed_id(am_catalogue[i].id)) {
            am_result_t observed=select_app(c,i,false);
            if(observed==AM_NOT_FOUND)continue; // No source or installed namespace: do not create one.
            if(observed==AM_OK)observed=am_store_recover(&c->store);
            if(observed==AM_OK)observed=refresh(c);
            if(observed!=AM_OK)c->local[i].disabled=true;
        }
        r=select_app(c,first,false);if(r==AM_OK)r=refresh(c);
    }
    if(r!=AM_OK) {
        am_result_t closed=close_store(c);
        c->state=r==AM_DISABLED && closed==AM_OK?AM_UNASSIGNED:AM_FAULT;
        if(closed!=AM_OK)r=closed;
    }
    return finish(c,r);
}
am_result_t am_controller_refresh(am_controller_t *c)
{
    if(!c)return AM_IO;
    if(c->busy)return AM_BUSY;
    if(c->state!=AM_READY)return AM_DISABLED;
    begin(c);return finish(c,refresh(c));
}
static am_result_t launch(am_controller_t *c)
{
    if(!strcmp(c->store.app_id,"doom")) {
        if(!c->platform.launch_doom)return AM_DISABLED;
        am_metadata_t meta;char path[AM_PATH_MAX];
        am_result_t r=am_store_doom_open(&c->store,&meta,&c->lease,path);
        if(r==AM_OK && !pending(c)) {
            mode(c,false);
            if(!pending(c))r=P(c,launch_doom,&c->store,&c->lease,&meta,path);
        }
        if(r==AM_BUSY){c->state=AM_FAULT;return r;}
        am_result_t released=release_execution(c);
        if(released!=AM_OK){c->state=AM_FAULT;return released;}
        if(c->home_pending)return AM_CANCELLED;
        mode(c,true);return r;
    }
    if(!c->platform.launch)return AM_DISABLED;
    am_metadata_t meta;
    am_result_t r=am_store_lookup(&c->store,&meta);
    if(r!=AM_OK)return r;
    uint8_t *copy=P(c,allocate,meta.size,true);
    if(!copy)return AM_IO;
    c->execution_copy=copy;
    size_t capacity=meta.size;
    r=am_store_snapshot(&c->store,&meta,copy,capacity,&c->lease);
    if(r==AM_OK && !pending(c)) {
        mode(c,false); // All files closed; immediate OS Home during opaque native execution.
        if(!pending(c))r=P(c,launch,copy,meta.size,&meta);
    }
    if(r==AM_BUSY){c->state=AM_FAULT;return r;}
    am_result_t released=release_execution(c);
    if(released!=AM_OK){c->state=AM_FAULT;return released;}
    if(c->home_pending)return AM_CANCELLED;
    mode(c,true); // Subsequent refresh/quiesce again runs on the app task under scoped deferral.
    return r;
}
static am_result_t fetch(am_controller_t *c)
{
    if(!c->local[c->selected].fetch_available)return AM_DISABLED;
    mode(c,false); // No live FS writer/lease during network acquisition and verification.
    if(pending(c))return AM_CANCELLED;
    am_result_t r=P(c,fetch,am_controller_cancel,c);
    if(pending(c))return AM_CANCELLED;
    if(c->state==AM_READY) {
        mode(c,true);
        if(pending(c))return AM_CANCELLED;
        am_result_t observed=refresh(c);if(observed!=AM_OK)r=observed;
    } else availability(c,&c->local[c->selected],NULL);
    return r;
}
static am_result_t acquire(am_controller_t *c,bool fetched)
{
    if(!fetched && c->platform.progress)P(c,progress,"Verifying...");
    am_package_t input={0};am_metadata_t next,current={0};
    am_result_t r=candidate(c,&input,&next);
    if(r!=AM_OK)return r;
    r=am_store_lookup(&c->store,&current);
    bool identical=false;
    if(r==AM_OK || r==AM_DISABLED) {
        if(!newer(&next,&current))
            identical=r==AM_OK && !newer(&current,&next) && !strcmp(current.hash,next.hash);
        if(!newer(&next,&current) && !identical)return AM_OLD_VERSION;
    } else if(r!=AM_NOT_FOUND)return r;
    if(pending(c))return AM_CANCELLED;
    if(c->platform.prepare) {
        am_result_t prepared=P(c,prepare,&c->store,am_controller_cancel,c);
        if(prepared!=AM_OK && prepared!=AM_UP_TO_DATE)return prepared;
    }
    if(identical)return pending(c)?AM_CANCELLED:AM_UP_TO_DATE;
    if(c->platform.progress)P(c,progress,r==AM_NOT_FOUND?"Installing...":"Updating...");
    if(pending(c))return AM_CANCELLED;
    return am_store_install(&c->store,input);
}
am_result_t am_controller_action(am_controller_t *c,unsigned selected,enum am_action action)
{
    if(!c || selected>=AM_COUNT)return AM_IO;
    if(c->busy)return AM_BUSY;
    if(action==AM_DETAILS)return AM_OK;
    if(action==AM_FETCH && !strcmp(am_catalogue[selected].id,"doom"))return AM_DISABLED;
    if(!am_managed_id(am_catalogue[selected].id))return AM_DISABLED;
    if(c->exit_requested || (c->state!=AM_READY && c->state!=AM_UNASSIGNED))return AM_DISABLED;
    if(action==AM_FETCH && !c->local[selected].fetch_available)return AM_DISABLED;
    if(action!=AM_FETCH && (c->state!=AM_READY || !c->platform.home_end))return AM_DISABLED;
    if(c->lease.owner || c->execution_copy)return AM_BUSY;
    begin(c);am_result_t r=pending(c)?AM_CANCELLED:
        select_app(c,selected,action==AM_INSTALL || action==AM_UPDATE);
    if(r!=AM_OK)return finish(c,r);
    if(action==AM_FETCH) {
        return finish(c,fetch(c));
    }
    if(c->state!=AM_READY || !c->platform.home_end)return finish(c,AM_DISABLED);
    r=AM_DISABLED;
    if(am_controller_cancel(c))r=AM_CANCELLED;
    else if(action==AM_OPEN)r=launch(c);
    else if(action==AM_UNINSTALL)r=am_store_uninstall(&c->store);
    else if(action==AM_INSTALL || action==AM_UPDATE) {
        bool online=c->platform.source_ready;
        r=online?fetch(c):AM_OK;
        if(r==AM_OK && !pending(c))r=acquire(c,online);
    }
    if(!strcmp(c->store.app_id,"doom") && c->platform.discard && !c->lease.owner)P0(c,discard);
    // Reopen actual receipts even after cancellation/full/write errors, never fake UI state.
    if(c->state==AM_READY && !c->lease.owner) {
        mode(c,true);
        am_result_t observed=refresh(c);
        if(observed!=AM_OK)r=observed;
    }
    return finish(c,r);
}
am_result_t am_controller_shutdown(am_controller_t *c)
{
    if(!c)return AM_IO;
    if(c->busy)return AM_BUSY;
    c->busy=true;
    if(c->execution_copy || c->lease.owner) {
        mode(c,false); // No writer or descriptor: a stalled drawer must keep immediate Home.
        am_result_t r=release_execution(c);
        if(r!=AM_OK){c->state=AM_FAULT;return finish(c,r);}
    }
    mode(c,true); // Scope deferral to actual filesystem close/discard only.
    return finish(c,close_store(c));
}
const char *am_controller_notice(am_result_t r)
{
    switch(r) {
    case AM_OK:return NULL;
    case AM_DISABLED:return "Unavailable";
    case AM_FULL:return "Storage full";
    case AM_BUSY:return "App busy";
    case AM_CANCELLED:return "Cancelled";
    case AM_WIFI_FAILED:return "Wi-Fi connection failed";
    case AM_TIME_FAILED:return "Time sync failed";
    case AM_DOWNLOAD_FAILED:return "Download failed";
    case AM_UP_TO_DATE:return "Up to date";
    case AM_OLD_VERSION:return "Version not newer";
    case AM_BAD_AUTH:case AM_BAD_HASH:case AM_BAD_MANIFEST:return "Package rejected";
    default:return "Storage unavailable";
    }
}
