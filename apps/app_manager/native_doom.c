// Doom-only borrowed IWAD, scoped engine heap, synchronous frame copy and fixed save slots.
#include <math.h>
#include <string.h>
#include "esp_memory_utils.h"
#include "badge/badge.h"
#include "native_host_private.h"
#include "native_doom_private.h"
typedef struct allocation {
    struct allocation *previous,*next;size_t size;max_align_t data[];
} allocation_t;
static struct {
    am_native_doom_t io;badge_doom_api_t api;badge_doom_lease_t wad;
    allocation_t *head;size_t total;uint32_t generation;bool bound;
} doom;
static bool permitted(void *ctx)
{
    am_store_t *s=doom.io.store;const am_lease_t *l=doom.io.lease;
    return doom.bound && am_native_access(ctx,AM_NATIVE_DOOM,false) && s && l && s->lease==l &&
        l->owner==s && l->generation==doom.generation && s->generation==doom.generation;
}
static bool ram(const void *p,size_t n)
{
    uintptr_t a=(uintptr_t)p;if(!a || !n || a>UINTPTR_MAX-(n-1))return false;
    const void *last=(const void *)(a+n-1);
    return esp_ptr_byte_accessible(p) && esp_ptr_byte_accessible(last) &&
        ((esp_ptr_in_dram(p) && esp_ptr_in_dram(last)) || (esp_ptr_external_ram(p) && esp_ptr_external_ram(last)));
}
static int32_t get_lease(void *ctx,badge_doom_lease_t *out)
{
    if(!permitted(ctx))return 0;
    if((uintptr_t)out%_Alignof(badge_doom_lease_t) || !ram(out,sizeof *out)){am_native_fault();return 0;}
    *out=doom.wad;return 1;
}
static void *resize(void *ctx,void *pointer,uint32_t bytes)
{
    if(!permitted(ctx))return NULL;
    allocation_t *old=doom.head;
    if(pointer) {while(old && old->data!=pointer)old=old->next;if(!old){am_native_fault();return NULL;}}
    else old=NULL;
    if(bytes>3u*1024*1024)return NULL;
    size_t original=old?old->size+offsetof(allocation_t,data):0;
    size_t charged=bytes?(size_t)bytes+offsetof(allocation_t,data):0;
    if(charged>3u*1024*1024-(doom.total-original))return NULL;
    allocation_t *prev=old?old->previous:NULL,*next=old?old->next:doom.head;
    if(!bytes) {
        if(!old)return NULL;
        if(prev)prev->next=next;else doom.head=next;
        if(next)next->previous=prev;
        doom.total-=original;doom.io.free(old);return NULL;
    }
    allocation_t *out=doom.io.resize(old,offsetof(allocation_t,data)+(size_t)bytes);
    if(!out)return NULL;
    out->previous=prev;out->next=next;out->size=bytes;
    if(prev)prev->next=out;else doom.head=out;
    if(next)next->previous=out;
    doom.total=doom.total-original+charged;return out->data;
}
static int32_t present(void *ctx,const uint8_t *indices,const uint8_t *rgb)
{
    if(!permitted(ctx))return 0;
    if(!ram(indices,BADGE_DOOM_FRAME_BYTES) || !ram(rgb,BADGE_DOOM_RGB_BYTES)){am_native_fault();return 0;}
    const badge_native_api_t *a=&doom.api.base;if(!a->begin_frame(ctx))return 0;
    // Consume both guest buffers before submit; the drawer sees only resident fb.
    uint8_t colors[768],lum[256];memcpy(colors,rgb,sizeof colors);
    for(unsigned i=0;i<256;i++) {
        float l=((0.299f*colors[i*3]+0.587f*colors[i*3+1]+0.114f*colors[i*3+2])/255-0.05f)/(0.45f-0.05f);
        lum[i]=(uint8_t)(255*powf(l<0?0:l>1?1:l,0.8f));
    }
    static const uint8_t bayer[4][4]={{8,136,40,168},{200,72,232,104},{56,184,24,152},{248,120,216,88}};
    for(unsigned y=0;y<600;y++) {
        const uint8_t *row=indices+(y/3)*320,*threshold=bayer[y&3];uint8_t *dst=fb+(TOP+y)*(W/8);
        for(unsigned x=0;x<960;x+=8) {
            uint8_t bits=0;for(unsigned k=x;k<x+8;k++)bits=(uint8_t)(bits<<1 | (lum[row[k/3]]<threshold[k&3]));
            *dst++=bits;
        }
    }
    a->submit_frame(ctx);return 1;
}
static void save_path(uint32_t slot,char path[32])
{
    const char *fixed[]={"data/doomsav0.dsg","data/doomsav1.dsg","data/doomsav2.dsg","data/doomsav3.dsg",
        "data/doomsav4.dsg","data/doomsav5.dsg","data/recovery.dsg"};strcpy(path,fixed[slot]);
}
static int32_t save_read(void *ctx,uint32_t slot,uint32_t offset,uint8_t *out,uint32_t bytes)
{
    if(!permitted(ctx))return -1;
    if(slot>BADGE_DOOM_RECOVERY_SAVE || !bytes || bytes>BADGE_DOOM_READ_MAX || offset>BADGE_DOOM_SAVE_MAX ||
       bytes>BADGE_DOOM_SAVE_MAX-offset || !ram(out,bytes)){am_native_fault();return -1;}
    char path[32];save_path(slot,path);size_t got=0;am_store_t *s=doom.io.store;
    am_result_t r=s->io.read_at(s->io.context,path,offset,out,bytes,&got);
    return r==AM_OK && got<=bytes?(int32_t)got:-1;
}
static int32_t save_replace(void *ctx,uint32_t slot,const uint8_t *data,uint32_t bytes)
{
    if(!permitted(ctx))return 0;
    if(slot>BADGE_DOOM_RECOVERY_SAVE || !bytes || bytes>BADGE_DOOM_SAVE_MAX || !ram(data,bytes)){am_native_fault();return 0;}
    am_store_t *s=doom.io.store;am_io_t *i=&s->io;char path[32];save_path(slot,path);
    am_result_t r=i->mkdir(i->context,"data");
    if(r==AM_OK)r=i->remove(i->context,"data/save-next");
    for(uint32_t off=0;r==AM_OK && off<bytes;) {
        uint32_t n=bytes-off;if(n>4096)n=4096;r=i->write(i->context,"data/save-next",off,data+off,n);off+=n;
    }
    if(r==AM_OK)r=i->sync(i->context,"data/save-next");
    if(r==AM_OK)r=i->sync(i->context,"data");
    if(r==AM_OK)r=i->move(i->context,"data/save-next",path,true);
    if(r==AM_OK)r=i->sync(i->context,"data");
    return r==AM_OK;
}
am_result_t am_doom_bind(const am_native_doom_t *io,const badge_native_api_t *base)
{
    if(doom.bound)return AM_BUSY;
    if(!io || !base || !io->store || strcmp(io->store->app_id,"doom") || !io->lease ||
       !io->begin || !io->resize || !io->free || !io->finish)return AM_DISABLED;
    doom.io=*io;doom.generation=io->lease->generation;
    if(io->begin(io->context,&doom.wad)!=1 || !doom.wad.wad || doom.wad.wad_bytes!=BADGE_DOOM_WAD_BYTES || doom.wad.zone_mib!=2) {
        io->finish(io->context);memset(&doom,0,sizeof doom);return AM_DISABLED;
    }
    doom.api=(badge_doom_api_t){.base=*base,.get_lease=get_lease,.resize=resize,.present=present,.save_read=save_read,.save_replace=save_replace};
    doom.api.base.struct_size=sizeof doom.api;doom.bound=true;return AM_OK;
}
const badge_native_api_t *am_doom_get(uint32_t n)
{ return doom.bound && (n<=sizeof(badge_native_api_t) || n==sizeof doom.api)?&doom.api.base:NULL; }
void am_doom_end(void)
{
    if(!doom.bound)return;
    while(doom.head){allocation_t *p=doom.head;doom.head=p->next;doom.io.free(p);}
    doom.io.finish(doom.io.context);memset(&doom,0,sizeof doom);
}
