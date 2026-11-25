// Fixed Messages capabilities; shared service and portal memory never belong to the guest.
#include <string.h>
#include "esp_memory_utils.h"
#include "badge/badge.h"
#include "native_messages_private.h"
#include "native_host_private.h"
static struct {
    am_native_messages_t io;badge_messages_api_t api;
    bool bound,closed,available,attempted;char labels[4][21];
} messages;
static bool permitted(void *ctx,bool frame)
{return messages.bound && am_native_access(ctx,AM_NATIVE_MESSAGES,frame) && !messages.closed;}
static bool writable(const void *p,size_t n)
{
    uintptr_t first=(uintptr_t)p;if(!first || !n || first>UINTPTR_MAX-(n-1))return false;
    const void *last=(const void *)(first+n-1);
    return esp_ptr_byte_accessible(p) && esp_ptr_byte_accessible(last) &&
        ((esp_ptr_in_dram(p) && esp_ptr_in_dram(last)) || (esp_ptr_external_ram(p) && esp_ptr_external_ram(last)));
}
static bool readable(const void *p,size_t n)
{
    uintptr_t first=(uintptr_t)p;if(!first || !n || first>UINTPTR_MAX-(n-1))return false;
    const void *last=(const void *)(first+n-1);
    return writable(p,n) || (esp_ptr_in_drom(p) && esp_ptr_in_drom(last));
}
static bool copy_label(const char *s,char out[21])
{
    if(!s){out[0]=0;return true;}
    for(unsigned n=0;n<=20;n++) {
        if((uintptr_t)s>UINTPTR_MAX-n || !readable((const void *)((uintptr_t)s+n),1))return false;
        out[n]=s[n];if(!out[n])return true;
    }
    return false;
}
static int32_t start(void *ctx)
{return permitted(ctx,false)?am_native_info(ctx,AM_NATIVE_MESSAGES,messages.io.start,messages.io.context):0;}
static int32_t key(void *ctx,uint32_t timeout)
{
    if(!permitted(ctx,false))return BADGE_MSG_NO_KEY;
    return am_native_key(ctx,timeout,!messages.available && !messages.attempted,false);
}
static void keys(void *ctx,const char *p,const char *m,const char *a,const char *b)
{
    if(!permitted(ctx,true))return;
    char copied[4][21]={{0}};const char *values[]={p,m,a,b};
    for(unsigned i=0;i<4;i++)if(!copy_label(values[i],copied[i])){am_native_fault();return;}
    memcpy(messages.labels,copied,sizeof copied);
    ui_keys(messages.labels[0][0]?messages.labels[0]:NULL,messages.labels[1][0]?messages.labels[1]:NULL,
            messages.labels[2][0]?messages.labels[2]:NULL,messages.labels[3][0]?messages.labels[3]:NULL);
}
static int32_t snapshot(void *ctx,message_state_t *out,uint64_t *revision)
{
    if(!permitted(ctx,false))return 0;
    if((uintptr_t)out%_Alignof(message_state_t) || !writable(out,sizeof *out) ||
       (revision && ((uintptr_t)revision%_Alignof(uint64_t) || !writable(revision,sizeof *revision))))
        {am_native_fault();return 0;}
    messages.available=messages.io.snapshot(messages.io.context,out,revision)==1;
    if(messages.available && (out->count>MSG_CAP || out->ignore_count>MSG_IGNORED || out->relay>1))
        {messages.available=false;am_native_fault();}
    return messages.available;
}
static int32_t read_thread(void *ctx,const uint8_t *id)
{
    if(!permitted(ctx,false))return MSG_IO;
    if(!readable(id,16)){am_native_fault();return MSG_IO;}
    uint8_t copied[16];memcpy(copied,id,sizeof copied);
    int32_t r=messages.io.read_thread(messages.io.context,copied);
    if(r<MSG_OK || r>MSG_DELETED){am_native_fault();return MSG_IO;}return r;
}
static int32_t status(void *ctx,char *out,uint32_t capacity)
{
    if(!permitted(ctx,false))return -1;
    if(capacity>256 || !writable(out,capacity)){am_native_fault();return -1;}
    out[0]=0;int32_t n=messages.io.status(messages.io.context,out,capacity);out[capacity-1]=0;
    if(n<0 || (uint32_t)n>=capacity){am_native_fault();return -1;}return n;
}
static uint64_t utc_now(void *ctx){return permitted(ctx,false)?messages.io.utc_now(messages.io.context):0;}
static int32_t portal_start(void *ctx,const uint8_t *id)
{
    if(!permitted(ctx,false))return 0;
    uint8_t copied[16];
    if(id){if(!readable(id,16)){am_native_fault();return 0;}memcpy(copied,id,16);}
    messages.attempted=true; // Latch even a partial setup failure: original Home/reset lifetime.
    return messages.io.portal_start(messages.io.context,id?copied:NULL)==1;
}
static int32_t portal_view(void *ctx,badge_msg_phone_t *out)
{
    if(!permitted(ctx,false))return 0;
    if(!writable(out,sizeof *out)){am_native_fault();return 0;}
    *out=(badge_msg_phone_t){0};int32_t r=messages.io.portal_view(messages.io.context,out);
    out->ssid[19]=out->password[16]=0;return r==1;
}
static int32_t info(void *ctx)
{
    if(!permitted(ctx,false))return 0;
    return am_native_info(ctx,AM_NATIVE_MESSAGES,messages.io.info,messages.io.context);
}
static int32_t release(void *ctx)
{
    if(!permitted(ctx,false) || messages.attempted)return 0;
    if(messages.io.finish(messages.io.context)!=AM_OK)return 0;
    messages.closed=true;am_native_note_release(AM_NATIVE_MESSAGES);return 1;
}
am_result_t am_messages_bind(const am_native_messages_t *io,const badge_native_api_t *base)
{
    if(!io || !base || !io->begin || !io->start || !io->snapshot || !io->read_thread || !io->status ||
       !io->utc_now || !io->portal_start || !io->portal_view || !io->info || !io->finish)return AM_DISABLED;
    if(messages.bound)return AM_BUSY;
    am_result_t r=io->begin(io->context);if(r!=AM_OK)return r;
    memset(&messages,0,sizeof messages);messages.io=*io;messages.bound=true;
    messages.api=(badge_messages_api_t){.base=*base,.start=start,.key=key,.keys=keys,.snapshot=snapshot,
        .read_thread=read_thread,.status=status,.utc_now=utc_now,.portal_start=portal_start,.portal_view=portal_view,
        .info=info,.release=release};messages.api.base.struct_size=sizeof messages.api;return AM_OK;
}
const badge_native_api_t *am_messages_get(uint32_t minimum)
{return messages.bound && !messages.closed && (minimum<=sizeof(badge_native_api_t) || minimum==sizeof messages.api)?&messages.api.base:NULL;}
am_result_t am_messages_end(void)
{
    if(!messages.bound)return AM_OK;
    if(messages.attempted)return AM_BUSY;
    am_result_t r=messages.io.finish(messages.io.context);if(r!=AM_OK)return r;
    messages.bound=false;messages.io=(am_native_messages_t){0};ui_keys(NULL,NULL,NULL,NULL);
    memset(messages.labels,0,sizeof messages.labels);return AM_OK;
}
