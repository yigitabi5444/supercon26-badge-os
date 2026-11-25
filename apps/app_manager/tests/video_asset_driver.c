// Actual asset transaction, VFS, OpenSSL and lease. File-backed bounded source, no target execution.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../video_asset.h"
#include "../host_c/adapters.h"

typedef struct { FILE *file;const char *mode;unsigned calls,max; } source_t;
static am_result_t source_read(void *ctx,uint32_t offset,uint8_t *out,size_t cap,size_t *got)
{
    source_t *source=ctx;assert(cap<=AM_ASSET_CHUNK);source->calls++;
    if(cap>source->max)source->max=(unsigned)cap;
    if(!strcmp(source->mode,"cancel") && offset>=8192)return AM_CANCELLED;
    if(fseek(source->file,(long)offset,SEEK_SET))return AM_IO;
    *got=fread(out,1,cap,source->file);if(ferror(source->file))return AM_IO;
    if(!strcmp(source->mode,"tamper") && offset<=4000 && *got>4000-offset)out[4000-offset]^=1;
    return AM_OK;
}
static size_t small_file(const char *folder,const char *name,uint8_t *out,size_t cap)
{
    char path[AM_VFS_PATH_MAX];int n=snprintf(path,sizeof path,"%s/%s",folder,name);
    assert(n>0 && (size_t)n<sizeof path);FILE *f=fopen(path,"rb");assert(f);
    size_t got=fread(out,1,cap,f);assert(fgetc(f)==EOF && !ferror(f));assert(!fclose(f));return got;
}
static am_result_t read_scope(am_video_store_t *video)
{
    am_store_t *s=video->store;am_lease_t lease={0},other={0};am_video_asset_t asset;
    am_result_t r=am_store_read_lease(s,&lease);assert(r==AM_OK);
    r=am_video_asset_open(video,&lease,AM_VIDEO_CLIP_HASH,&asset);
    if(r==AM_OK) {
        uint8_t bytes[AM_ASSET_CHUNK];
        assert(am_video_asset_read(&asset,0,bytes,12)==AM_OK && !memcmp(bytes,"VID2",4));
        assert(am_video_asset_read(&asset,AM_VIDEO_CLIP_BYTES-1,bytes,1)==AM_OK);
        assert(am_video_asset_read(&asset,UINT32_MAX,bytes,1)==AM_LIMIT);
        assert(am_video_asset_read(&asset,AM_VIDEO_CLIP_BYTES-1,bytes,2)==AM_LIMIT);
        assert(am_video_asset_read(&asset,0,bytes,sizeof bytes+1)==AM_LIMIT);
        assert(am_store_read_lease(s,&other)==AM_BUSY);
        assert(am_store_uninstall(s)==AM_BUSY && am_store_recover(s)==AM_BUSY);
        am_video_asset_t rejected;
        assert(am_video_asset_open(video,&lease,"wrong-hash",&rejected)==AM_DISABLED);
        s->app_id="aliens";
        assert(am_video_asset_read(&asset,0,bytes,1)==AM_DISABLED);
        assert(am_video_asset_open(video,&lease,AM_VIDEO_CLIP_HASH,&rejected)==AM_DISABLED);
        s->app_id="video";
        assert(am_store_release(s,&lease)==AM_OK);
        assert(am_video_asset_read(&asset,0,bytes,1)==AM_DISABLED);
        assert(am_store_read_lease(s,&lease)==AM_OK);
        assert(am_video_asset_read(&asset,0,bytes,1)==AM_DISABLED); // Same token address, new generation.
        assert(am_video_asset_open(video,&lease,AM_VIDEO_CLIP_HASH,&asset)==AM_OK);
    }
    assert(am_store_release(s,&lease)==AM_OK);return r;
}
int main(int argc,char **argv)
{
    assert(argc==7); // root public-key receipt-folder source-file mode owner-id
    am_host_t host;am_store_t s={.app_id=argv[6],.runtime={"esp32s3","6.0.2","2.1.0",2}};
    s.work=calloc(1,sizeof *s.work);assert(s.work);
    assert(am_host_io(&host,argv[1],2*1024*1024,&s.io)==AM_OK);
    assert(am_openssl_crypto(argv[2],&s.crypto)==AM_OK);
    am_video_store_t video={&s,am_openssl_sha256_stream};source_t source={.mode=argv[5]};
    am_result_t r;
    if(!strcmp(argv[5],"open"))r=read_scope(&video);
    else {
        uint8_t bytes[AM_VIDEO_RECEIPT_BYTES+1],signature[AM_SIGNATURE_MAX];
        size_t n=small_file(argv[3],"clip.receipt",bytes,sizeof bytes);
        size_t m=small_file(argv[3],"clip.sig",signature,sizeof signature);
        source.file=fopen(argv[4],"rb");assert(source.file);
        r=am_video_asset_install(&video,(am_bytes_t){bytes,n},(am_bytes_t){signature,m},source_read,&source);
        assert(!fclose(source.file));
    }
    assert(host.lock_fd==-1 && !s.lease);
    printf("{\"result\":%d,\"source_calls\":%u,\"largest_chunk\":%u}\n",r,source.calls,source.max);
    am_openssl_close(&s.crypto);free(s.work);return 0;
}
