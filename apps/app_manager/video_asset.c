#include <string.h>
#include "video_asset.h"

#define IO(s,fn,...) ((s)->io.fn((s)->io.context,__VA_ARGS__))
#define CURRENT "assets/" AM_VIDEO_CLIP_HASH
#define STAGE "stage/asset"
static const uint8_t digest[32]={
    0xc8,0x04,0xc2,0xcb,0xb5,0xdf,0x28,0x30,0xf1,0x7a,0x81,0x4d,0x65,0xbe,0x2f,0xab,
    0x22,0x9b,0x08,0x25,0xd6,0xe8,0x9d,0x08,0x9a,0x64,0x55,0x1d,0x5d,0x6d,0x6d,0xf7};
static bool configured(const am_video_store_t *v)
{
    const am_store_t *s=v?v->store:NULL;
    return s && s->app_id && !strcmp(s->app_id,"video") && s->work && v->hash &&
        s->crypto.verify_p256 && s->io.lock && s->io.read && s->io.read_at && s->io.write &&
        s->io.mkdir && s->io.move && s->io.sync && s->io.remove && s->io.space;
}
static am_result_t checkpoint(am_store_t *s,am_checkpoint_t point)
{ return s->io.checkpoint?IO(s,checkpoint,point):AM_OK; }
static am_result_t leave(am_store_t *s,am_result_t r)
{
    am_result_t unlocked=IO(s,lock,false);
    return unlocked!=AM_OK && (r==AM_OK || r==AM_UP_TO_DATE)?unlocked:r;
}
static am_result_t receipt(am_store_t *s,am_bytes_t bytes,am_bytes_t signature)
{
    if(!bytes.data || bytes.size!=AM_VIDEO_RECEIPT_BYTES)return AM_BAD_MANIFEST;
    uint8_t raw[64];am_result_t r=am_signature_raw(signature,raw);
    if(r==AM_OK)r=s->crypto.verify_p256(s->crypto.context,bytes.data,bytes.size,raw);
    if(r!=AM_OK)return r;
    const uint8_t *p=bytes.data;static const char owner[16]="video";
    uint32_t size=(uint32_t)p[20]|(uint32_t)p[21]<<8|(uint32_t)p[22]<<16|(uint32_t)p[23]<<24;
    return !memcmp(p,"BVA1",4) && !memcmp(p+4,owner,16) && size==AM_VIDEO_CLIP_BYTES &&
        !memcmp(p+24,digest,32)?AM_OK:AM_BAD_MANIFEST;
}
typedef struct { am_store_t *store;const char *path; } reader_t;
static am_result_t read_file(void *ctx,uint32_t offset,uint8_t *out,size_t cap,size_t *got)
{
    reader_t *reader=ctx;am_result_t r=checkpoint(reader->store,AM_AFTER_CHUNK);
    return r==AM_OK?IO(reader->store,read_at,reader->path,offset,out,cap,got):r;
}
static am_result_t verify_file(am_video_store_t *v,const char *path)
{
    reader_t reader={v->store,path};uint8_t actual[32];
    am_result_t r=v->hash(read_file,&reader,AM_VIDEO_CLIP_BYTES,actual);
    return r==AM_OK && memcmp(actual,digest,32)?AM_BAD_HASH:r;
}
static am_result_t current(am_video_store_t *v)
{
    uint8_t bytes[AM_VIDEO_RECEIPT_BYTES],signature[AM_SIGNATURE_MAX];size_t n=0,m=0;
    am_store_t *s=v->store;
    am_result_t r=IO(s,read,CURRENT "/receipt",bytes,sizeof bytes,&n);
    if(r!=AM_OK)return r;
    r=IO(s,read,CURRENT "/signature",signature,sizeof signature,&m);
    if(r==AM_NOT_FOUND)return AM_BAD_RECORD;
    if(r==AM_OK)r=receipt(s,(am_bytes_t){bytes,n},(am_bytes_t){signature,m});
    if(r==AM_OK)r=verify_file(v,CURRENT "/clip.vid");
    return r==AM_NOT_FOUND?AM_BAD_RECORD:r;
}
am_result_t am_video_asset_check(am_video_store_t *v)
{
    if(!configured(v))return AM_DISABLED;
    am_store_t *s=v->store;if(s->lease)return AM_BUSY;
    am_result_t r=IO(s,lock,true);if(r!=AM_OK)return r;
    return leave(s,current(v));
}
static am_result_t download(am_store_t *s,am_reader_t read,void *ctx)
{
    uint8_t *chunk=s->work->payload;
    for(uint32_t offset=0;offset<AM_VIDEO_CLIP_BYTES;) {
        am_result_t r=checkpoint(s,AM_AFTER_CHUNK);if(r!=AM_OK)return r;
        size_t n=AM_VIDEO_CLIP_BYTES-offset;if(n>AM_ASSET_CHUNK)n=AM_ASSET_CHUNK;
        size_t got=0;r=read(ctx,offset,chunk,n,&got);if(r!=AM_OK)return r;
        if(got!=n)return AM_BAD_HASH;
        r=IO(s,write,STAGE "/clip.vid",offset,chunk,n);if(r!=AM_OK)return r;
        offset+=(uint32_t)n;
    }
    size_t got=0;am_result_t r=read(ctx,AM_VIDEO_CLIP_BYTES,chunk,1,&got);
    return r==AM_OK && got?AM_BAD_HASH:r;
}
am_result_t am_video_asset_install(am_video_store_t *v,am_bytes_t bytes,am_bytes_t signature,
                                   am_reader_t source,void *context)
{
    if(!configured(v) || !source)return AM_DISABLED;
    am_store_t *s=v->store;
    am_result_t r=receipt(s,bytes,signature);if(r!=AM_OK)return r;
    if(s->lease)return AM_BUSY;
    r=IO(s,lock,true);if(r!=AM_OK)return r;
    r=current(v);
    if(r==AM_OK)return leave(s,AM_UP_TO_DATE); // Immutable current is reused without any source read/write.
    if(r!=AM_NOT_FOUND)return leave(s,r);
    r=IO(s,remove,STAGE); // Reclaim only this abandoned asset stage before sizing a retry.
    if(r!=AM_OK)return leave(s,r);
    uint64_t used=0,total=0,need=s->reserve_bytes?s->reserve_bytes:4096;
    const size_t sizes[]={AM_VIDEO_CLIP_BYTES,bytes.size,signature.size};
    for(unsigned i=0;i<3;i++) {
        uint64_t n=sizes[i],unit=s->allocation_unit;
        if(unit)n=((n+unit-1)/unit)*unit;
        need+=n;
    }
    r=IO(s,space,&used,&total);
    if(r==AM_OK && (used>total || need>total-used))r=AM_FULL;
    if(r==AM_OK)r=IO(s,mkdir,"assets");
    if(r==AM_OK)r=IO(s,mkdir,STAGE);
    if(r==AM_OK)r=IO(s,write,STAGE "/receipt",0,bytes.data,bytes.size);
    if(r==AM_OK)r=IO(s,write,STAGE "/signature",0,signature.data,signature.size);
    if(r==AM_OK)r=download(s,source,context);
    if(r==AM_OK)r=IO(s,sync,STAGE "/clip.vid");
    if(r==AM_OK)r=IO(s,sync,STAGE "/receipt");
    if(r==AM_OK)r=IO(s,sync,STAGE "/signature");
    if(r==AM_OK)r=IO(s,sync,STAGE);
    if(r==AM_OK)r=verify_file(v,STAGE "/clip.vid");
    if(r==AM_OK)r=checkpoint(s,AM_BEFORE_ACTIVATE);
    if(r==AM_OK)r=IO(s,move,STAGE,CURRENT,false);
    bool published=r==AM_OK;
    if(r==AM_OK)r=checkpoint(s,AM_AFTER_ACTIVATE); // Finish durability after the immutable publish commit.
    if(r==AM_OK)r=IO(s,sync,"assets");
    if(r==AM_OK)r=IO(s,sync,"");
    if(r==AM_OK)r=current(v);
    if(!published) {
        am_result_t cleaned=IO(s,remove,STAGE);
        if(r==AM_OK)r=cleaned;
    }
    return leave(s,r);
}
static bool scoped(const am_store_t *s,const am_lease_t *lease,uint32_t generation)
{
    return s && s->app_id && !strcmp(s->app_id,"video") && lease && s->lease==lease &&
        lease->owner==s && lease->generation==generation && s->generation==generation;
}
am_result_t am_video_asset_open(am_video_store_t *v,const am_lease_t *lease,const char *hash,am_video_asset_t *out)
{
    if(out)memset(out,0,sizeof *out);
    if(!configured(v) || !hash || strcmp(hash,AM_VIDEO_CLIP_HASH) || !out)return AM_DISABLED;
    if(!lease || !scoped(v->store,lease,lease->generation))return AM_BAD_RECORD;
    am_result_t r=current(v);
    if(r==AM_OK)*out=(am_video_asset_t){v->store,lease,lease->generation};
    return r;
}
am_result_t am_video_asset_read(const am_video_asset_t *asset,uint32_t offset,uint8_t *out,size_t size)
{
    if(!am_video_asset_live(asset))return AM_DISABLED;
    if(!out || !size || size>AM_ASSET_CHUNK || offset>AM_VIDEO_CLIP_BYTES || size>AM_VIDEO_CLIP_BYTES-offset)
        return AM_LIMIT;
    size_t got=0;am_result_t r=IO(asset->store,read_at,CURRENT "/clip.vid",offset,out,size,&got);
    return r==AM_OK && got!=size?AM_BAD_HASH:r;
}
bool am_video_asset_live(const am_video_asset_t *asset)
{ return asset && scoped(asset->store,asset->lease,asset->generation); }
