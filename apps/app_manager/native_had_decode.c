// Original Hackaday resize/Atkinson path, with bounded input and cancellation.
#include <string.h>
#include "esp_heap_caps.h"
#include "stb_image.h"
#include "native_had_job.h"

bool am_had_decode(am_had_job_t *job,const uint8_t *raw,uint32_t n)
{
    int sw,sh,ch;
    if(!raw || !n || n>=BADGE_HAD_TRANSFER_MAX || !job->box_w || !job->box_h ||
       job->box_w>960 || job->box_h>544 || atomic_load(&job->cancel) ||
       !stbi_info_from_memory(raw,(int)n,&sw,&sh,&ch) || sw<1 || sh<1 || sw>4096 || sh>4096 ||
       (uint64_t)sw*(uint32_t)sh>4*1024*1024)return false;
    int iw=sw,ih=sh;
    uint8_t *src=stbi_load_from_memory(raw,(int)n,&sw,&sh,&ch,1);
    if(!src)return false;
    if(sw!=iw || sh!=ih || atomic_load(&job->cancel)){stbi_image_free(src);return false;}
    uint32_t w=job->box_w,h=(uint32_t)((uint64_t)sh*w/(uint32_t)sw);
    if(h>job->box_h){h=job->box_h;w=(uint32_t)((uint64_t)sw*h/(uint32_t)sh);}
    if(!w)w=1;
    if(!h)h=1;
    uint32_t stride=(w+7)/8;
    int16_t *gray=heap_caps_malloc(w*h*sizeof *gray,MALLOC_CAP_SPIRAM);
    uint8_t *bits=heap_caps_calloc(stride*h,1,MALLOC_CAP_SPIRAM);
    bool ok=gray && bits;
    for(uint32_t y=0;ok && y<h;y++) {
        if(atomic_load(&job->cancel)){ok=false;break;}
        for(uint32_t x=0;x<w;x++) {
            uint32_t x0=x*(uint32_t)sw/w,x1=(x+1)*(uint32_t)sw/w,y0=y*(uint32_t)sh/h,y1=(y+1)*(uint32_t)sh/h;
            uint32_t sum=0,count=0;
            for(uint32_t yy=y0;yy<y1 || yy==y0;yy++)for(uint32_t xx=x0;xx<x1 || xx==x0;xx++)
                {sum+=src[yy*(uint32_t)sw+xx];count++;}
            gray[y*w+x]=(int16_t)(sum/count);
        }
    }
    stbi_image_free(src);
    static const int8_t dx[]={1,2,-1,0,1,0},dy[]={0,0,1,1,1,2};
    for(uint32_t y=0;ok && y<h;y++) {
        if(atomic_load(&job->cancel)){ok=false;break;}
        for(uint32_t x=0;x<w;x++) {
            int16_t *p=&gray[y*w+x];int v=*p<128?0:255,err=(*p-v)/8;
            if(!v)bits[y*stride+x/8]|=(uint8_t)(0x80u>>(x&7));
            for(unsigned k=0;k<6;k++) {
                int xx=(int)x+dx[k],yy=(int)y+dy[k];
                if(xx>=0 && (uint32_t)xx<w && (uint32_t)yy<h)gray[(uint32_t)yy*w+(uint32_t)xx]+=(int16_t)err;
            }
        }
    }
    heap_caps_free(gray);
    if(!ok){heap_caps_free(bits);return false;}
    job->data=bits;job->width=w;job->height=h;job->length=stride*h;return true;
}
static uint32_t crc32(const uint8_t *p,uint32_t n)
{
    uint32_t crc=UINT32_MAX;
    while(n--){crc^=*p++;for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));}
    return ~crc;
}
static uint32_t little(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
bool am_had_inflate(am_had_job_t *job,const uint8_t *raw,uint32_t n)
{
    if(!raw || n<18 || n>=BADGE_HAD_TRANSFER_MAX || raw[0]!=0x1f || raw[1]!=0x8b || raw[2]!=8 || (raw[3]&0xe0))return false;
    uint32_t pos=10,end=n-8;
    if(raw[3]&4) {
        if(end-pos<2)return false;
        uint32_t length=(uint32_t)raw[pos]|(uint32_t)raw[pos+1]<<8;pos+=2;
        if(length>end-pos)return false;
        pos+=length;
    }
    for(unsigned flag=8;flag<=16;flag*=2)if(raw[3]&flag) {
        while(pos<end && raw[pos])pos++;
        if(pos==end)return false;
        pos++;
    }
    if(raw[3]&2){if(end-pos<2 || (crc32(raw,pos)&65535u)!=((uint32_t)raw[pos]|(uint32_t)raw[pos+1]<<8))return false;pos+=2;}
    if(pos>=end || atomic_load(&job->cancel))return false;
    uint8_t *data=heap_caps_malloc(BADGE_HAD_FEED_MAX,MALLOC_CAP_SPIRAM);if(!data)return false;
    int count=stbi_zlib_decode_noheader_buffer((char *)data,BADGE_HAD_FEED_MAX-1,(const char *)raw+pos,(int)(end-pos));
    if(count<0 || atomic_load(&job->cancel) || (uint32_t)count!=little(raw+n-4) || crc32(data,(uint32_t)count)!=little(raw+n-8))
        {heap_caps_free(data);return false;}
    data[count]=0;job->data=data;job->length=(uint32_t)count;return true;
}
