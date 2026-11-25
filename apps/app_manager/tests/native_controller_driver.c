#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "../controller.h"
#include "../host_c/adapters.h"
#include "../package_source.h"
#include "../managed_apps.h"

typedef struct {
    am_host_t host;am_controller_t *controller;const char *root,*key,*folder;
    am_workspace_t *input;bool deferred,pending,hold,fail_release;
    bool online,cached,full,cancel_fetch,cancel_install,multiple,opened;unsigned fetched,installs,updates;
    const char *selected_id;
    am_result_t (*space_base)(void *,uint64_t *,uint64_t *);
    am_source_t source;unsigned payload_reads;
    size_t execution_size;unsigned allocated,freed,launched,quiesced;
} fixture_t;
static am_result_t shared_space(void *ctx,uint64_t *used,uint64_t *total)
{
    fixture_t *f=ctx;*used=0;*total=f->host.vfs.quota;
    // Sum app roots under one quota, like target mount info. The bounded VFS
    // paths start inside each namespace; an extra volume prefix is not an app path.
    for(unsigned i=0;i<AM_COUNT;i++)if(am_managed_id(am_catalogue[i].id)) {
        am_vfs_t volume={.quota=*total};struct stat st;
        int n=snprintf(volume.root,sizeof volume.root,"%s/%s",f->root,am_catalogue[i].id);
        if(n<0 || (size_t)n>=sizeof volume.root)return AM_LIMIT;
        if(stat(volume.root,&st)){if(errno==ENOENT)continue;return AM_IO;}
        uint64_t bytes,capacity;am_result_t r=f->space_base(&volume,&bytes,&capacity);
        if(r!=AM_OK)return r;
        if(UINT64_MAX-*used<bytes)return AM_LIMIT;
        *used+=bytes;
    }
    return AM_OK;
}
static am_result_t bind_store(fixture_t *f,am_store_t *s,const char *id)
{
    char path[AM_VFS_BASE_MAX];
    int n=snprintf(path,sizeof path,"%s/%s",f->root,id);
    if(n<0 || (size_t)n>=sizeof path)return AM_LIMIT;
    am_result_t r=am_host_io(&f->host,path,131072,&s->io);
    if(r==AM_OK){f->space_base=s->io.space;s->io.space=shared_space;}
    return r;
}
static am_result_t select_app(void *ctx,am_store_t *s,const char *id,bool create,bool *ready)
{
    fixture_t *f=ctx;
    if(!am_managed_id(id))return AM_DISABLED;
    assert(!s->lease || !s->lease->owner);
    *ready=f->online;
    if(f->opened && !create) {
        char path[AM_VFS_BASE_MAX];struct stat st;
        int n=snprintf(path,sizeof path,"%s/%s",f->root,id);
        if(n<0 || (size_t)n>=sizeof path)return AM_LIMIT;
        if(stat(path,&st))return AM_NOT_FOUND;
    }
    am_result_t r=f->opened?bind_store(f,s,id):AM_OK;
    if(r==AM_OK){f->selected_id=id;f->cached=false;f->source.valid=false;*ready=f->online;}
    return r;
}
static am_result_t read_input(const char *folder,const char *name,uint8_t *out,size_t cap,size_t *size)
{
    char path[AM_VFS_PATH_MAX];int n=snprintf(path,sizeof path,"%s/%s",folder,name);
    if(n<0 || (size_t)n>=sizeof path)return AM_LIMIT;
    FILE *f=fopen(path,"rb");if(!f)return AM_IO;
    *size=fread(out,1,cap,f);int extra=fgetc(f),bad=ferror(f),closed=fclose(f);
    return extra!=EOF?AM_LIMIT:bad || closed?AM_IO:AM_OK;
}
static am_result_t incoming(void *ctx,am_package_t *p)
{
    fixture_t *f=ctx;if(!strcmp(f->folder,"-") || (f->online && !f->cached))return AM_DISABLED;
    if(f->multiple)return am_source_cached(&f->source,p);
    size_t n[3]={0};uint8_t *b[]={f->input->manifest,f->input->signature,f->input->payload};
    const size_t caps[]={AM_MANIFEST_MAX,AM_SIGNATURE_MAX,AM_PAYLOAD_MAX};
    const char *names[]={"manifest","signature","module.elf"};
    for(unsigned i=0;i<3;i++){am_result_t r=read_input(f->folder,names[i],b[i],caps[i],&n[i]);if(r!=AM_OK)return r;}
    *p=(am_package_t){{b[0],n[0]},{b[1],n[1]},{b[2],n[2]}};return AM_OK;
}
static am_result_t get(void *ctx,const char *url,uint8_t *out,size_t cap,size_t *size)
{
    fixture_t *f=ctx;const char *name=strrchr(url,'/');assert(name);
    if(!strcmp(name+1,"module.elf"))f->payload_reads++;
    return read_input(f->folder,name+1,out,cap,size);
}
static am_result_t fetch(void *ctx,bool (*cancel)(void *),void *owner)
{
    fixture_t *f=ctx;assert(!f->deferred && f->controller->busy && f->host.lock_fd==-1);
    assert(!cancel(owner));f->fetched++;f->cached=true;
    am_package_t p;am_metadata_t meta;char token[65];am_result_t r;
    if(f->multiple) {
        f->source.app_id=f->selected_id;
        r=am_source_fetch(&f->source,&f->controller->store.crypto,&f->controller->store.runtime,
                          get,f,cancel,owner);
    } else {
        r=incoming(ctx,&p);
        if(r==AM_OK)r=am_package_integrity(&f->controller->store.crypto,p,&meta,token);
    }
    f->cached=r==AM_OK;
    if(r==AM_OK && f->full) {
        uint64_t used,total;assert(f->controller->store.io.space(&f->host,&used,&total)==AM_OK);
        f->host.vfs.quota=used+1;
    }
    if(f->cancel_fetch)f->pending=true;
    return r;
}
static void progress(void *ctx,const char *message)
{
    fixture_t *f=ctx;assert(f->deferred && f->host.lock_fd==-1);
    if(!strcmp(message,"Installing..."))f->installs++;
    if(!strcmp(message,"Updating..."))f->updates++;
    if(f->cancel_install)f->pending=true;
}
static am_result_t open_store(void *ctx,am_store_t *s,bool (*cancel)(void *),void *owner)
{
    (void)cancel;(void)owner;fixture_t *f=ctx;
    am_result_t r=f->multiple?bind_store(f,s,s->app_id):am_host_io(&f->host,f->root,131072,&s->io);
    if(r==AM_OK)r=am_openssl_crypto(f->key,&s->crypto);
    f->opened=r==AM_OK;
    return r;
}
static am_result_t close_store(void *ctx)
{
    fixture_t *f=ctx;assert(f->host.lock_fd==-1);
    am_openssl_close(&f->controller->store.crypto);return AM_OK;
}
static void *allocate(void *ctx,size_t size,bool execution)
{
    fixture_t *f=ctx;void *p=malloc(size);if(p)f->allocated++;
    if(execution)f->execution_size=size;
    return p;
}
static void release(void *ctx,void *p){fixture_t *f=ctx;f->freed++;free(p);}
static void mode(void *ctx,bool value){((fixture_t *)ctx)->deferred=value;}
static bool requested(void *ctx)
{fixture_t *f=ctx;bool value=f->pending;f->pending=false;return value;}
static bool end(void *ctx)
{fixture_t *f=ctx;f->deferred=false;return requested(ctx);}
static am_result_t quiesce(void *ctx)
{fixture_t *f=ctx;assert(!f->launched || !f->deferred);f->quiesced++;return f->hold?AM_BUSY:AM_OK;}
static am_result_t launch(void *ctx,const uint8_t *copy,size_t size,const am_metadata_t *meta)
{
    fixture_t *f=ctx;assert(!f->deferred && f->controller->lease.owner && copy && size==meta->size);
    assert(f->execution_size==size && !strcmp(meta->id,f->multiple?f->selected_id:"aliens"));
    assert(am_store_uninstall(&f->controller->store)==AM_BUSY);
    if(f->multiple)assert(am_controller_action(f->controller,0,AM_UNINSTALL)==AM_BUSY);
    f->launched++;return f->hold?AM_BUSY:AM_OK;
}
int main(int argc,char **argv)
{
    if(argc!=5 && argc!=6)return 2;
    fixture_t f={.root=argv[1],.key=argv[2],.folder=argv[4]};am_controller_t c;
    f.multiple=argc==6;
    static const uint8_t test_public[65]={4}; // Structural source double; crypto uses the actual disposable PEM.
    static const am_source_config_t config={.public_key=test_public,.key_size=65,
        .manifest_url="https://packages.example/manifest",.signature_url="https://packages.example/signature",
        .payload_url="https://packages.example/module.elf"};
    if(f.multiple){f.source.config=&config;f.source.bytes=calloc(1,sizeof *f.source.bytes);assert(f.source.bytes);}
    f.online=!strncmp(argv[3],"one_",4);f.full=!strcmp(argv[3],"one_full");
    f.cancel_fetch=!strcmp(argv[3],"one_cancel");f.cancel_install=!strcmp(argv[3],"one_cancel_install");
    f.controller=&c;f.input=calloc(1,sizeof *f.input);assert(f.input);
    am_platform_t p={.context=&f,.assigned=true,.open=open_store,.close=close_store,.incoming=incoming,
        .allocate=allocate,.free=release,.home_mode=mode,.home_request=requested,.home_end=end,
        .launch=launch,.quiesce=quiesce,.source_ready=f.online,.fetch=fetch,.progress=progress,
        .select=f.multiple?select_app:NULL};
    am_result_t r=am_controller_init(&c,p,(am_runtime_t){"esp32s3","6.0.2","2.1.0",2});
    unsigned id=0;for(;id<AM_COUNT;id++)if(!strcmp(am_catalogue[id].id,f.multiple?argv[5]:"aliens"))break;
    assert(id<AM_COUNT);
    if(r==AM_OK) {
        if(f.online)r=am_controller_action(&c,id,!strcmp(argv[3],"one_install")?AM_INSTALL:AM_UPDATE);
        else if(!strcmp(argv[3],"install"))r=am_controller_action(&c,id,AM_INSTALL);
        else if(!strcmp(argv[3],"uninstall"))r=am_controller_action(&c,id,AM_UNINSTALL);
        else if(!strcmp(argv[3],"open") || !strcmp(argv[3],"busy")) {
            f.hold=!strcmp(argv[3],"busy");r=am_controller_action(&c,id,AM_OPEN);
            if(f.hold) {
                assert(r==AM_BUSY && c.state==AM_FAULT && c.execution_copy && c.lease.owner);
                unsigned freed=f.freed;
                assert(am_controller_shutdown(&c)==AM_BUSY && f.freed==freed && c.execution_copy && c.lease.owner);
                f.hold=false;
            } else assert(!c.execution_copy && !c.lease.owner);
        } else if(!strcmp(argv[3],"source"))r=am_controller_action(&c,0,AM_INSTALL);
        else if(!strcmp(argv[3],"lookup"))r=c.local[id].installed?AM_OK:AM_NOT_FOUND;
        else r=AM_IO;
    }
    bool installed=c.local[id].installed;char version[21];memcpy(version,c.version[id],sizeof version);
    bool ai=c.local[3].installed,ri=c.local[4].installed;
    char av[21],rv[21];memcpy(av,c.version[3],21);memcpy(rv,c.version[4],21);
    assert(am_controller_shutdown(&c)==AM_OK && !c.execution_copy && !c.lease.owner && !f.deferred);
    assert(f.allocated==f.freed);
    printf("{\"result\":%d,\"installed\":%s,\"version\":\"%s\",\"launched\":%u,\"execution_size\":%zu,\"fetches\":%u,\"installs\":%u,\"updates\":%u,\"aliens_installed\":%s,\"rocks_installed\":%s,\"aliens_version\":\"%s\",\"rocks_version\":\"%s\",\"payload_reads\":%u}\n",
        r,installed?"true":"false",version,f.launched,f.execution_size,f.fetched,f.installs,f.updates,
        ai?"true":"false",ri?"true":"false",av,rv,f.payload_reads);
    free(f.input);free(f.source.bytes);return 0;
}
