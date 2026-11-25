// Fixed Hackaday capabilities and scoped memory, never a general app allocator.
#include <string.h>
#include "esp_memory_utils.h"
#include "badge/badge.h"
#include "native_hackaday_private.h"
#include "native_host_private.h"

void am_had_fault(void) { am_native_fault(); }
bool am_had_access(void *ctx,bool frame) { return am_native_access(ctx,AM_NATIVE_HACKADAY,frame); }
void am_had_note_release(void) { am_native_note_release(AM_NATIVE_HACKADAY); }
int32_t am_had_key(void *ctx,uint32_t timeout)
{
    if(!am_had_access(ctx,false))return BADGE_HAD_NO_KEY;
    if(timeout>500){am_had_fault();return BADGE_HAD_NO_KEY;}
    return am_native_key(ctx,timeout,false,am_had_closing());
}

static struct {
    am_native_hackaday_t io;badge_hackaday_api_t api;
    struct { void *pointer;uint32_t size; } heap[BADGE_HAD_ALLOCATIONS_MAX];
    uint32_t total,job,width,height;bool bound,closed,closing;char labels[4][21],live_labels[4][21];
} had;
static bool permitted(void *ctx,bool frame)
{ return had.bound && am_had_access(ctx,frame) && !had.closed; }
static bool writable(const void *p,uint32_t n)
{
    uintptr_t first=(uintptr_t)p;if(!first || !n || first>UINTPTR_MAX-(n-1))return false;
    const void *last=(const void *)(first+n-1);
    return esp_ptr_byte_accessible(p) && esp_ptr_byte_accessible(last) &&
        ((esp_ptr_in_dram(p) && esp_ptr_in_dram(last)) || (esp_ptr_external_ram(p) && esp_ptr_external_ram(last)));
}
static bool copy_string(const char *s,char *out,uint32_t max)
{
    if(!s){out[0]=0;return true;}
    uintptr_t first=(uintptr_t)s;
    for(uint32_t n=0;n<=max;n++) {
        if(first>UINTPTR_MAX-n)return false;
        const char *p=(const char *)(first+n);
        if(!esp_ptr_in_drom(p) && !(esp_ptr_byte_accessible(p) && (esp_ptr_in_dram(p)||esp_ptr_external_ram(p))))return false;
        out[n]=*p;if(!out[n])return true;
    }
    return false;
}
static void *allocate(void *ctx,uint32_t n)
{
    if(!permitted(ctx,false) || had.closing)return NULL;
    if(!n || n>BADGE_HAD_FEED_MAX || n>BADGE_HAD_HEAP_MAX-had.total)return NULL;
    for(unsigned i=0;i<BADGE_HAD_ALLOCATIONS_MAX;i++)if(!had.heap[i].pointer) {
        void *p=had.io.allocate(had.io.context,n);if(!p)return NULL;
        had.heap[i].pointer=p;had.heap[i].size=n;had.total+=n;return p;
    }
    return NULL;
}
static void deallocate(void *ctx,void *p)
{
    if(!permitted(ctx,false) || !p)return;
    for(unsigned i=0;i<BADGE_HAD_ALLOCATIONS_MAX;i++)if(had.heap[i].pointer==p) {
        had.io.deallocate(had.io.context,p);had.total-=had.heap[i].size;
        had.heap[i].pointer=NULL;had.heap[i].size=0;return;
    }
    am_had_fault();
}
static bool owned(const void *p,uint32_t n)
{
    uintptr_t first=(uintptr_t)p;
    for(unsigned i=0;i<BADGE_HAD_ALLOCATIONS_MAX;i++) {
        uintptr_t start=(uintptr_t)had.heap[i].pointer;
        if(start && first>=start && first-start<=had.heap[i].size && n<=had.heap[i].size-(first-start))return true;
    }
    return false;
}
static void keys(void *ctx,const char *p,const char *m,const char *a,const char *b)
{
    if(!permitted(ctx,false))return;
    const char *values[]={p,m,a,b};char copy[4][21]={{0}};
    for(unsigned i=0;i<4;i++)if(!copy_string(values[i],copy[i],20)){am_had_fault();return;}
    memcpy(had.labels,copy,sizeof copy); // OS labels change only when the next frame owns the mutex.
}
void am_had_apply_keys(void)
{
    memcpy(had.live_labels,had.labels,sizeof had.labels);
    ui_keys(had.live_labels[0][0]?had.live_labels[0]:NULL,had.live_labels[1][0]?had.live_labels[1]:NULL,
            had.live_labels[2][0]?had.live_labels[2]:NULL,had.live_labels[3][0]?had.live_labels[3]:NULL);
}
bool am_had_closing(void) { return had.closing; }
static int32_t begin_feed(void *ctx)
{
    if(!permitted(ctx,false) || had.closing || had.job)return 0;
    if(had.io.begin_feed(had.io.context)!=1)return 0;
    had.job=1;return 1;
}
static int32_t begin_image(void *ctx,const char *url,uint32_t w,uint32_t h)
{
    if(!permitted(ctx,false) || had.closing || had.job)return 0;
    char copy[400];
    if(!url || !copy_string(url,copy,399) || !w || !h || w>960 || h>544){am_had_fault();return 0;}
    if(had.io.begin_image(had.io.context,copy,w,h)!=1)return 0;
    had.job=2;had.width=w;had.height=h;return 1;
}
static int32_t poll(void *ctx,badge_had_result_t *out)
{
    if(!permitted(ctx,false))return BADGE_HAD_FAILED;
    if((uintptr_t)out%_Alignof(badge_had_result_t) || !writable(out,sizeof *out)){am_had_fault();return BADGE_HAD_FAILED;}
    *out=(badge_had_result_t){0};if(!had.job)return BADGE_HAD_FAILED;
    badge_had_result_t r={0};int32_t status=had.io.poll(had.io.context,&r);r.error[63]=0;
    bool good=r.wifi_joined<=1 && r.join_ms<=20000 && r.bytes<BADGE_HAD_TRANSFER_MAX;
    if(status==BADGE_HAD_OK) {
        if(had.job==1)good=good && r.length<BADGE_HAD_FEED_MAX && writable(r.data,r.length+1) && !r.data[r.length] && !r.width && !r.height;
        else good=good && r.width && r.height && r.width<=had.width && r.height<=had.height &&
            r.length==((r.width+7)/8)*r.height && writable(r.data,r.length);
    } else {r.data=NULL;r.length=r.width=r.height=0;good=good && (status==BADGE_HAD_PENDING || status==BADGE_HAD_FAILED);}
    if(!good){am_had_fault();return BADGE_HAD_FAILED;}
    *out=r;return status;
}
static int32_t end_job(void *ctx,uint32_t close)
{
    if(!permitted(ctx,false))return 0;
    if(close>1){am_had_fault();return 0;}
    if(had.io.end_job(had.io.context,close)!=1){had.closing=true;am_had_fault();return 0;}
    had.job=0;return 1;
}
static void blit(void *ctx,int32_t x,int32_t y,uint32_t w,uint32_t h,const uint8_t *bits,uint32_t n)
{
    if(!permitted(ctx,true))return;
    uint32_t stride=(w+7)/8;
    if(!w || !h || w>960 || h>544 || x<0 || y<TOP || (uint32_t)x>960-w || (uint32_t)y>H-h ||
       n!=stride*h || !owned(bits,n)){am_had_fault();return;}
    for(uint32_t row=0;row<h;row++) {
        if(!(x&7))memcpy(fb+((uint32_t)y+row)*(W/8)+(uint32_t)x/8,bits+row*stride,stride);
        else for(uint32_t col=0;col<w;col++)if(bits[row*stride+col/8]&(0x80u>>(col&7))) {
            uint32_t pixel=((uint32_t)y+row)*W+(uint32_t)x+col;fb[pixel/8]|=(uint8_t)(0x80u>>(pixel&7));
        }
    }
}
static int32_t release(void *ctx)
{
    if(!permitted(ctx,false))return 0;
    if(!end_job(ctx,1) || had.total){am_had_fault();return 0;}
    had.closed=true;am_had_note_release();return 1;
}
am_result_t am_had_bind(const am_native_hackaday_t *io,const badge_native_api_t *base)
{
    if(!io || !base || !io->begin || !io->allocate || !io->deallocate || !io->begin_feed || !io->begin_image ||
       !io->poll || !io->end_job || !io->finish)return AM_DISABLED;
    if(had.bound)return AM_BUSY;
    am_result_t r=io->begin(io->context);if(r!=AM_OK)return r;
    memset(&had,0,sizeof had);had.io=*io;had.bound=true;
    had.api=(badge_hackaday_api_t){.base=*base,.key=am_had_key,.keys=keys,.alloc=allocate,.dealloc=deallocate,
        .begin_feed=begin_feed,.begin_image=begin_image,.poll=poll,.end_job=end_job,.blit_mono=blit,.release=release};
    had.api.base.struct_size=sizeof had.api;return AM_OK;
}
const badge_native_api_t *am_had_get(uint32_t minimum)
{ return had.bound && !had.closed && (minimum<=sizeof(badge_native_api_t) || minimum==sizeof had.api)?&had.api.base:NULL; }
am_result_t am_had_end(void)
{
    if(!had.bound)return AM_OK;
    am_result_t r=had.io.finish(had.io.context);if(r!=AM_OK)return r;
    for(unsigned i=0;i<BADGE_HAD_ALLOCATIONS_MAX;i++)if(had.heap[i].pointer)had.io.deallocate(had.io.context,had.heap[i].pointer);
    memset(had.heap,0,sizeof had.heap);had.total=had.job=had.width=had.height=0;
    had.bound=false;had.io=(am_native_hackaday_t){0};ui_keys(NULL,NULL,NULL,NULL);
    memset(had.labels,0,sizeof had.labels);memset(had.live_labels,0,sizeof had.live_labels);return AM_OK;
}
