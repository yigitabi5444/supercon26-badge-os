#include <string.h>
#include "video_source.h"

const am_video_source_config_t am_video_source_config __attribute__((weak))={0};
static bool same_origin(const char *a,const char *b)
{
    if(!a || !b)return false;
    const char *x=strchr(a+8,'/'),*y=strchr(b+8,'/');
    return x && y && x-a==y-b && !memcmp(a,b,(size_t)(x-a));
}
bool am_video_source_valid(const am_video_source_config_t *c)
{
    if(!c || !am_source_config_valid(c->code))return false;
    const char *urls[]={c->receipt_url,c->signature_url,c->clip_url};
    for(unsigned i=0;i<3;i++)
        if(!am_source_url_allowed(c->code,urls[i]) || !same_origin(c->code->manifest_url,urls[i]))return false;
    return true;
}
typedef struct { am_https_t *http;const char *url;am_https_video_t stream; } reader_t;
static am_result_t read_clip(void *ctx,uint32_t offset,uint8_t *out,size_t cap,size_t *got)
{
    reader_t *reader=ctx;
    if(!reader->stream.client) {
        if(offset)return AM_DISABLED;
        am_result_t r=am_https_video_open(reader->http,reader->url,&reader->stream);
        if(r!=AM_OK)return r;
    }
    return am_https_video_read(&reader->stream,offset,out,cap,got);
}
am_result_t am_video_source_prepare(am_video_store_t *store,const am_video_source_config_t *c,am_https_t *http,
                                     bool (*cancel)(void *),void *owner,am_progress_t progress)
{
    if(!http || !cancel)return AM_IO;
    am_result_t r=am_video_asset_check(store);
    if(r==AM_OK)return AM_OK; // Offline reuse across code versions; no radio or duplicate asset.
    if(r!=AM_NOT_FOUND)return r;
    if(!am_video_source_valid(c))return AM_DISABLED;
    r=am_https_begin(http,c->code,cancel,owner,progress);
    uint8_t receipt[AM_VIDEO_RECEIPT_BYTES],signature[AM_SIGNATURE_MAX];size_t n=0,m=0;
    if(r==AM_OK)r=am_https_get(http,c->receipt_url,receipt,sizeof receipt,&n);
    if(r==AM_OK)r=am_https_get(http,c->signature_url,signature,sizeof signature,&m);
    reader_t reader={.http=http,.url=c->clip_url};
    if(r==AM_OK)r=am_video_asset_install(store,(am_bytes_t){receipt,n},(am_bytes_t){signature,m},read_clip,&reader);
    am_result_t closed=am_https_video_close(&reader.stream);
    am_https_end(http);
    return r==AM_OK || r==AM_UP_TO_DATE?closed:r;
}
