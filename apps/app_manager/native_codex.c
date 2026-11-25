#include <string.h>
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "badge/badge.h"
#include "native_codex_private.h"
#include "native_host_private.h"

void am_codex_fault(void) { am_native_fault(); }
bool am_codex_access(void *ctx,bool frame) { return am_native_access(ctx,AM_NATIVE_CODEX,frame); }
void am_codex_note_release(void) { am_native_note_release(AM_NATIVE_CODEX); }
int32_t am_codex_button(void *ctx,uint32_t timeout)
{
    if(!am_codex_access(ctx,false))return BADGE_CODEX_NONE;
    if(timeout>100){am_codex_fault();return BADGE_CODEX_NONE;}
    int key=am_native_key(ctx,timeout,true,false);
    return key>=0 && key<=3?key+1:BADGE_CODEX_NONE;
}

static struct { am_native_codex_t io;badge_codex_api_t api;bool bound,closed;char labels[4][21]; } codex;
#define IO(fn,...) (codex.io.fn(codex.io.context, __VA_ARGS__))
static bool access(void *ctx,bool frame)
{ return codex.bound && am_codex_access(ctx,frame) && !codex.closed; }
static bool writable(void *p,uint32_t n)
{
    uintptr_t first=(uintptr_t)p;if(!first || !n || first>UINTPTR_MAX-(n-1))return false;
    const void *last=(const void *)(first+n-1);
    return esp_ptr_byte_accessible(p) && esp_ptr_byte_accessible(last) &&
        ((esp_ptr_in_dram(p) && esp_ptr_in_dram(last)) || (esp_ptr_external_ram(p) && esp_ptr_external_ram(last)));
}
static bool copied_string(const char *s,char *out,uint32_t max)
{
    if(!s){if(out)out[0]=0;return true;}
    uintptr_t start=(uintptr_t)s;
    for(uint32_t n=0;n<=max;n++) {
        if(start>UINTPTR_MAX-n)return false;
        const char *p=(const char *)(start+n);
        if(!esp_ptr_in_drom(p) && !(esp_ptr_byte_accessible(p) && (esp_ptr_in_dram(p)||esp_ptr_external_ram(p))))return false;
        char c=*p;if(out)out[n]=c;if(!c)return true;
    }
    return false;
}
static uint32_t mode(void *ctx){return access(ctx,false)?codex.io.mode(codex.io.context):BADGE_CODEX_INVALID;}
static uint64_t now(void *ctx)
{return access(ctx,false)?(uint64_t)esp_timer_get_time():0;}
static void red(void *ctx,uint32_t lit)
{
    if(!access(ctx,false))return;
    if(lit>1){am_codex_fault();return;}IO(red_leds,lit);
}
static int32_t seen_load(void *ctx,char *out,uint32_t cap)
{
    if(!access(ctx,false))return 0;
    if(cap>BADGE_CODEX_SEEN_MAX || !writable(out,cap)){am_codex_fault();return 0;}
    out[0]=0;int32_t r=IO(seen_load,out,cap);out[cap-1]=0;return r==1;
}
static int32_t seen_save(void *ctx,const char *value)
{
    if(!access(ctx,false))return 0;
    if(!value || !copied_string(value,NULL,BADGE_CODEX_SEEN_MAX-1)){am_codex_fault();return 0;}
    return IO(seen_save,value)==1;
}
static int32_t join(void *ctx){return access(ctx,false)?codex.io.lan_join(codex.io.context):0;}
static int32_t fetch(void *ctx,char *out,uint32_t cap)
{
    if(!access(ctx,false))return BADGE_CODEX_FETCH_OFFLINE;
    if(cap!=BADGE_CODEX_BODY_MAX || !writable(out,cap)){am_codex_fault();return BADGE_CODEX_FETCH_INCOMPLETE;}
    int32_t r=IO(lan_fetch,out,cap);
    if(r>BADGE_CODEX_BODY_MAX || r<BADGE_CODEX_FETCH_INCOMPLETE){am_codex_fault();return BADGE_CODEX_FETCH_INCOMPLETE;}
    return r;
}
static int32_t usb_begin(void *ctx){return access(ctx,false)?codex.io.usb_begin(codex.io.context):0;}
static int32_t usb_read(void *ctx,char *out,uint32_t cap)
{
    if(!access(ctx,false))return 0;
    if(cap>BADGE_CODEX_USB_READ_MAX || !writable(out,cap)){am_codex_fault();return 0;}
    int32_t r=IO(usb_read,out,cap);if(r<0 || (uint32_t)r>cap){am_codex_fault();return 0;}return r;
}
static int32_t usb_ack(void *ctx,const char *revision)
{
    if(!access(ctx,false))return 0;
    char copy[33];if(!revision || !copied_string(revision,copy,32) || !copy[0]){am_codex_fault();return 0;}
    for(unsigned i=0;copy[i];i++)if(!((copy[i]>='0'&&copy[i]<='9') || (copy[i]>='a'&&copy[i]<='f') ||
        (copy[i]>='A'&&copy[i]<='F'))){am_codex_fault();return 0;}
    return IO(usb_ack,copy)==1;
}
static void release(void *ctx)
{
    if(!access(ctx,false))return;
    codex.io.release(codex.io.context);codex.closed=true;am_codex_note_release();
}
static void keys(void *ctx,const char *p,const char *m,const char *a,const char *b)
{
    if(!access(ctx,true))return;
    const char *values[]={p,m,a,b};char copied[4][21]={{0}};
    for(unsigned i=0;i<4;i++)if(!copied_string(values[i],copied[i],20)){am_codex_fault();return;}
    memcpy(codex.labels,copied,sizeof copied);
    ui_keys(codex.labels[0][0]?codex.labels[0]:NULL,codex.labels[1][0]?codex.labels[1]:NULL,
            codex.labels[2][0]?codex.labels[2]:NULL,codex.labels[3][0]?codex.labels[3]:NULL);
}
am_result_t am_codex_bind(const am_native_codex_t *io,const badge_native_api_t *base)
{
    if(!io || !base || !io->begin || !io->mode || !io->red_leds || !io->seen_load || !io->seen_save ||
       !io->lan_join || !io->lan_fetch || !io->usb_begin || !io->usb_read || !io->usb_ack || !io->release)return AM_DISABLED;
    if(codex.bound)return AM_BUSY;
    am_result_t r=io->begin(io->context);if(r!=AM_OK){io->release(io->context);return r;}
    codex.io=*io;codex.bound=true;codex.closed=false;
    codex.api=(badge_codex_api_t){.base=*base,.mode=mode,.now_us=now,.button=am_codex_button,.red_leds=red,
        .seen_load=seen_load,.seen_save=seen_save,.lan_join=join,.lan_fetch=fetch,.usb_begin=usb_begin,
        .usb_read=usb_read,.usb_ack=usb_ack,.release=release,.keys=keys};
    codex.api.base.struct_size=sizeof codex.api;return AM_OK;
}
const badge_native_api_t *am_codex_get(uint32_t minimum)
{
    if(!codex.bound || codex.closed || (minimum>sizeof(badge_native_api_t) && minimum!=sizeof codex.api))return NULL;
    return &codex.api.base;
}
void am_codex_end(void)
{
    if(codex.bound && !codex.closed)codex.io.release(codex.io.context);
    codex.io=(am_native_codex_t){0};codex.bound=false;ui_keys(NULL,NULL,NULL,NULL);
}
