#include <stdio.h>
#include <string.h>
#include "txn.h"

#define IO(s, call, ...) ((s)->io.call((s)->io.context, __VA_ARGS__))
#define TRY(expression) do { am_result_t result_=(expression); if(result_!=AM_OK)return result_; } while(0)
static bool configured(const am_store_t *s)
{
    return s && s->work && s->app_id && s->app_id[0] && s->crypto.sha256 && s->crypto.verify_p256 && s->runtime.target &&
        s->runtime.sdk && s->runtime.loader && s->io.lock && s->io.read && s->io.write &&
        s->io.mkdir && s->io.sync && s->io.move && s->io.remove && s->io.versions && s->io.space;
}
static bool aliases_workspace(am_store_t *s,const uint8_t *bytes,size_t size)
{
    uintptr_t a=(uintptr_t)bytes,b=(uintptr_t)s->work;
    return size && (a>=b?a-b<sizeof *s->work:b-a<size);
}
static am_result_t leave(am_store_t *s,am_result_t result)
{
    am_result_t unlock=IO(s,lock,false);
    return result==AM_OK?unlock:result;
}
static am_result_t checkpoint(am_store_t *s,am_checkpoint_t point)
{ return s->io.checkpoint?s->io.checkpoint(s->io.context,point):AM_OK; }
static am_result_t pointer(am_store_t *s,am_pointer_t *p)
{
    uint8_t bytes[512];size_t size=0;
    memset(p,0,sizeof *p);
    am_result_t r=IO(s,read,"active/app.json",bytes,sizeof bytes,&size);
    if(r==AM_NOT_FOUND)return AM_OK;
    if(r==AM_LIMIT)return AM_BAD_RECORD;
    return r==AM_OK?am_pointer_parse((am_bytes_t){bytes,size},p):r;
}
static am_result_t load(am_store_t *s,const char *base,am_metadata_t *meta,char token[65])
{
    char path[AM_PATH_MAX];size_t sizes[3]={0};
    const char *names[]={"manifest","signature","module.elf"};
    uint8_t *buffers[]={s->work->manifest,s->work->signature,s->work->payload};
    const size_t caps[]={AM_MANIFEST_MAX,AM_SIGNATURE_MAX,AM_PAYLOAD_SIZE};
    bool doom=!strcmp(s->app_id,"doom");am_result_t r=AM_OK;uint8_t *temporary=NULL;
    for(unsigned i=0;i<3;i++) {
        int n=snprintf(path,sizeof path,"%s/%s",base,names[i]);
        if(n<0 || (size_t)n>=sizeof path){r=AM_LIMIT;break;}
        if(i==2 && doom) {
            r=am_package_receipt(&s->crypto,(am_bytes_t){buffers[0],sizes[0]},(am_bytes_t){buffers[1],sizes[1]},meta,token);
            if(r!=AM_OK)break;
            if(strcmp(meta->id,"doom") || !s->doom_allocate || !s->doom_free || !s->io.read_at){r=AM_DISABLED;break;}
            temporary=s->doom_allocate(meta->size);if(!temporary){r=AM_IO;break;}buffers[2]=temporary;
            for(uint32_t off=0;r==AM_OK && off<meta->size;) {
                size_t got=0,amount=meta->size-off;if(amount>4096)amount=4096;
                r=IO(s,read_at,path,off,temporary+off,amount,&got);
                if(r==AM_OK && got!=amount)r=AM_BAD_HASH;
                off+=(uint32_t)amount;
            }
            uint8_t extra;size_t got=0;
            if(r==AM_OK)r=IO(s,read_at,path,meta->size,&extra,1,&got);
            if(r==AM_OK && got)r=AM_BAD_HASH;
            sizes[2]=meta->size;
        } else r=IO(s,read,path,buffers[i],caps[i],&sizes[i]);
        if(r==AM_LIMIT)r=AM_BAD_MANIFEST;
        if(r!=AM_OK)break;
    }
    am_package_t p={{buffers[0],sizes[0]},{buffers[1],sizes[1]},{buffers[2],sizes[2]}};
    if(r==AM_OK)r=am_package_integrity(&s->crypto,p,meta,token);
    if(temporary)s->doom_free(temporary);
    return r==AM_OK && strcmp(meta->id,s->app_id)?AM_BAD_RECORD:r;
}
static am_result_t version(am_store_t *s,const char *expected,am_metadata_t *meta)
{
    char base[AM_PATH_MAX],actual[65];
    if(!am_token_valid(expected))return AM_BAD_RECORD;
    snprintf(base,sizeof base,"versions/%s",expected);
    TRY(load(s,base,meta,actual));
    return strcmp(expected,actual)?AM_BAD_HASH:AM_OK;
}
static am_result_t inspect(am_store_t *s,am_pointer_t *p,am_metadata_t *current)
{
    TRY(pointer(s,p));
    if(!p->exists)return AM_OK;
    // Integrity is independent of the current OS ABI. Never discard disabled packages.
    am_result_t r=version(s,p->current,current);
    if(r!=AM_OK)return r==AM_NOT_FOUND?AM_IO:r;
    if(p->previous) {
        am_metadata_t prior;
        r=version(s,p->prior,&prior);
        if(r!=AM_OK)return r==AM_NOT_FOUND?AM_IO:r;
    }
    return AM_OK;
}
static am_result_t recover(am_store_t *s)
{
    am_pointer_t p;am_metadata_t meta;size_t count=0;
    TRY(inspect(s,&p,&meta)); // No cleanup before strict pointer + referenced integrity checks.
    TRY(IO(s,versions,s->work->versions,&count));
    if(count>AM_VERSIONS_MAX)return AM_LIMIT;
    for(size_t i=0;i<count;i++)if(!am_token_valid(s->work->versions[i]))return AM_BAD_RECORD;
    for(size_t i=0;i<count;i++) {
        const char *token=s->work->versions[i];char path[AM_PATH_MAX];
        if(p.exists && (!strcmp(token,p.current) || (p.previous && !strcmp(token,p.prior))))continue;
        snprintf(path,sizeof path,"versions/%s",token);
        TRY(IO(s,remove,path));
    }
    TRY(IO(s,sync,"versions"));
    TRY(IO(s,remove,"stage"));
    TRY(IO(s,mkdir,"stage"));
    TRY(IO(s,sync,"stage"));
    return IO(s,sync,"");
}
am_result_t am_store_recover(am_store_t *s)
{
    if(!configured(s))return AM_IO;
    am_result_t r=IO(s,lock,true);
    if(r!=AM_OK)return r;
    return leave(s,recover(s));
}
am_result_t am_store_lookup(am_store_t *s,am_metadata_t *meta)
{
    if(!configured(s) || !meta)return AM_IO;
    am_result_t r=IO(s,lock,true);
    if(r!=AM_OK)return r;
    am_pointer_t p;
    r=inspect(s,&p,meta);
    if(r==AM_OK)r=!p.exists?AM_NOT_FOUND:am_package_compatible(meta,&s->runtime)?AM_OK:AM_DISABLED;
    return leave(s,r);
}
static bool newer(const am_metadata_t *a,const am_metadata_t *b)
{
    for(unsigned i=0;i<3;i++)if(a->parts[i]!=b->parts[i])return a->parts[i]>b->parts[i];
    return false;
}
static am_result_t stage(am_store_t *s,am_package_t p,const char token[65])
{
    TRY(checkpoint(s,AM_BEFORE_STAGE));
    TRY(IO(s,write,"stage/manifest",0,p.manifest.data,p.manifest.size));
    TRY(IO(s,write,"stage/signature",0,p.signature.data,p.signature.size));
    for(size_t offset=0;offset<p.payload.size;offset+=512) {
        size_t n=p.payload.size-offset;if(n>512)n=512;
        TRY(IO(s,write,"stage/module.elf",offset,p.payload.data+offset,n));
        TRY(checkpoint(s,AM_AFTER_CHUNK));
    }
    TRY(IO(s,sync,"stage/module.elf"));
    TRY(IO(s,sync,"stage/manifest"));
    TRY(IO(s,sync,"stage/signature"));
    TRY(IO(s,sync,"stage"));
    TRY(checkpoint(s,AM_AFTER_PAYLOAD_SYNC));
    am_metadata_t checked;char checked_token[65];
    TRY(load(s,"stage",&checked,checked_token));
    if(strcmp(token,checked_token))return AM_BAD_HASH;
    return checkpoint(s,AM_AFTER_VERIFY);
}
static am_result_t install(am_store_t *s,am_package_t p,const am_metadata_t *next,const char token[65])
{
    am_pointer_t old;am_metadata_t prior;
    TRY(inspect(s,&old,&prior));
    if(old.exists && !newer(next,&prior))return AM_OLD_VERSION;
    TRY(recover(s)); // Reclaim abandoned staging before sizing a retry.
    uint64_t used,capacity,need=s->reserve_bytes?s->reserve_bytes:4096;
    const size_t sizes[]={p.manifest.size,p.signature.size,p.payload.size,192};
    for(unsigned i=0;i<4;i++) {
        uint64_t n=sizes[i],unit=s->allocation_unit;
        if(unit)n=((n+unit-1)/unit)*unit;
        need+=n;
    }
    TRY(IO(s,space,&used,&capacity));
    if(used>capacity || capacity-used<need)return AM_FULL;
    TRY(stage(s,p,token));
    char destination[AM_PATH_MAX];snprintf(destination,sizeof destination,"versions/%s",token);
    am_result_t existing=version(s,token,&prior);
    if(existing==AM_OK)TRY(IO(s,remove,"stage"));
    else if(existing==AM_NOT_FOUND)TRY(IO(s,move,"stage",destination,false));
    else return existing;
    TRY(IO(s,mkdir,"stage"));
    TRY(IO(s,sync,"versions"));
    TRY(IO(s,sync,""));
    TRY(checkpoint(s,AM_AFTER_PUBLISH));
    am_pointer_t active={.exists=true,.previous=old.exists};uint8_t record[192];
    memcpy(active.current,token,65);
    if(old.exists)memcpy(active.prior,old.current,65);
    size_t size=am_pointer_encode(&active,record);
    if(!size)return AM_BAD_RECORD;
    TRY(IO(s,write,"stage/active-next",0,record,size));
    TRY(IO(s,sync,"stage/active-next"));
    TRY(IO(s,sync,"stage"));
    TRY(checkpoint(s,AM_BEFORE_ACTIVATE));
    TRY(IO(s,move,"stage/active-next","active/app.json",true));
    TRY(checkpoint(s,AM_AFTER_ACTIVATE));
    TRY(IO(s,sync,"active"));
    TRY(IO(s,sync,"stage"));
    TRY(checkpoint(s,AM_AFTER_ACTIVE_SYNC));
    return recover(s);
}
am_result_t am_store_install(am_store_t *s,am_package_t p)
{
    if(!configured(s))return AM_IO;
    if(aliases_workspace(s,p.manifest.data,p.manifest.size) ||
       aliases_workspace(s,p.signature.data,p.signature.size) ||
       aliases_workspace(s,p.payload.data,p.payload.size))return AM_IO;
    am_metadata_t next;char token[65];
    am_result_t r=am_package_integrity(&s->crypto,p,&next,token);
    if(r!=AM_OK)return r;
    if(strcmp(next.id,s->app_id) || !am_package_compatible(&next,&s->runtime))return AM_DISABLED;
    r=IO(s,lock,true);
    if(r!=AM_OK)return r;
    return leave(s,install(s,p,&next,token));
}
static am_result_t uninstall(am_store_t *s)
{
    am_pointer_t p;am_metadata_t meta;
    TRY(inspect(s,&p,&meta));
    if(!p.exists)return AM_OK;
    TRY(checkpoint(s,AM_BEFORE_UNINSTALL));
    TRY(IO(s,remove,"active/app.json"));
    TRY(checkpoint(s,AM_AFTER_UNINSTALL));
    TRY(IO(s,sync,"active"));
    TRY(checkpoint(s,AM_AFTER_UNINSTALL_SYNC));
    return recover(s);
}
am_result_t am_store_uninstall(am_store_t *s)
{
    if(!configured(s))return AM_IO;
    am_result_t r=IO(s,lock,true);
    if(r!=AM_OK)return r;
    return leave(s,uninstall(s));
}
am_result_t am_store_snapshot(am_store_t *s,am_metadata_t *meta,uint8_t *out,size_t cap,am_lease_t *lease)
{
    if(!configured(s) || !meta || !out || !cap || cap>AM_PAYLOAD_MAX || !lease ||
       ((uintptr_t)out&3) || aliases_workspace(s,out,cap))return AM_IO;
    am_result_t r=IO(s,lock,true);
    if(r!=AM_OK)return r;
    am_pointer_t p;r=inspect(s,&p,meta);
    if(r==AM_OK && !p.exists)r=AM_NOT_FOUND;
    if(r==AM_OK && !am_package_compatible(meta,&s->runtime))r=AM_DISABLED;
    // inspect also reads the previous copy. Re-read current before copying execution bytes.
    if(r==AM_OK)r=version(s,p.current,meta);
    if(r!=AM_OK)return leave(s,r);
    if(s->generation==UINT32_MAX)return leave(s,AM_LIMIT);
    if(meta->size>cap)return leave(s,AM_LIMIT);
    memcpy(out,s->work->payload,meta->size);
    s->lease=lease;*lease=(am_lease_t){s,++s->generation};
    return AM_OK; // Deliberately keep logical lease/lock, with every file already closed.
}
am_result_t am_store_release(am_store_t *s,am_lease_t *lease)
{
    if(!configured(s) || !lease || s->lease!=lease || lease->owner!=s ||
       lease->generation!=s->generation)return AM_BAD_RECORD;
    am_result_t r=IO(s,lock,false);
    if(r==AM_OK){s->lease=NULL;memset(lease,0,sizeof *lease);}
    return r;
}
am_result_t am_store_read_lease(am_store_t *s,am_lease_t *lease)
{
    if(!configured(s) || !lease)return AM_IO;
    if(s->lease || lease->owner)return AM_BUSY;
    am_result_t r=IO(s,lock,true);
    if(r!=AM_OK)return r;
    if(s->generation==UINT32_MAX)return leave(s,AM_LIMIT);
    s->lease=lease;*lease=(am_lease_t){s,++s->generation};
    return AM_OK;
}

am_result_t am_store_doom_open(am_store_t *s,am_metadata_t *meta,am_lease_t *lease,char path[AM_PATH_MAX])
{
    if(!configured(s) || strcmp(s->app_id,"doom") || !meta || !lease || !path || !s->io.read_at)return AM_DISABLED;
    if(s->lease || lease->owner)return AM_BUSY;
    am_result_t r=IO(s,lock,true);if(r!=AM_OK)return r;
    am_pointer_t p;r=inspect(s,&p,meta);
    if(r==AM_OK && !p.exists)r=AM_NOT_FOUND;
    if(r==AM_OK && !am_package_compatible(meta,&s->runtime))r=AM_DISABLED;
    if(r==AM_OK)r=version(s,p.current,meta);
    if(r!=AM_OK)return leave(s,r);
    if(s->generation==UINT32_MAX)return leave(s,AM_LIMIT);
    snprintf(path,AM_PATH_MAX,"versions/%s/module.elf",p.current);
    s->lease=lease;*lease=(am_lease_t){s,++s->generation};return AM_OK;
}
