// Real Messages geometry/content from 9b73810, adapted to supplied ABI2. GPL-2.0.
// QR encoder is the existing Nayuki source; MIT terms remain in its source/header.
#include "render.h"
#include "qrcodegen.h"
enum { W=960,H=672,TOP=48 };
typedef struct { const badge_native_api_t *api;const msg_phone_view_t *phone;const char *status;msg_labels_t labels; } render_t;
static size_t length(const char *s) { size_t n=0;while(s[n])n++;return n; }
static bool terminated(const char *s,size_t n) { if(!s)return false;for(size_t i=0;i<n;i++)if(!s[i])return true;return false; }
static bool equal16(const uint8_t *a,const uint8_t *b) { for(int i=0;i<16;i++)if(a[i]!=b[i])return false;return true; }
static void copy_text(char *out,size_t cap,const char *s)
{ size_t i=0;while(i+1<cap && s[i]) { out[i]=s[i];i++; }out[i]=0; }
typedef struct { char *s;size_t cap,n; } fmt_t;
static void add(fmt_t *f,const char *s) { while(*s && f->n+1<f->cap)f->s[f->n++]=*s++;f->s[f->n]=0; }
// Fixed 64 iterations avoid an undeclared Xtensa compiler-runtime division import.
static uint64_t divide(uint64_t n,uint32_t d,uint32_t *remainder)
{ uint64_t q=0;uint32_t r=0;for(int i=63;i>=0;i--){r=(r<<1)|(uint32_t)(n>>i&1);if(r>=d){r-=d;q|=UINT64_C(1)<<i;}}if(remainder)*remainder=r;return q; }
static void number(fmt_t *f,uint64_t n,unsigned digits)
{ char tmp[24];unsigned i=0;do {uint32_t r;n=divide(n,10,&r);tmp[i++]=(char)('0'+r);}while(n);while(i<digits)tmp[i++]='0';while(i){char s[2]={tmp[--i],0};add(f,s);} }
static fmt_t format(char *s,size_t cap) { s[0]=0;return (fmt_t){s,cap,0}; }
static void rbox(render_t *r,int x,int y,int w,int h,bool ink) { r->api->box(r->api->context,x,y,w,h,ink); }
static void routline(render_t *r,int x,int y,int w,int h,int t) { r->api->outline(r->api->context,x,y,w,h,t,1); }
static void rtext(render_t *r,int x,int y,int scale,const char *s,bool ink) { r->api->text(r->api->context,x,y,scale,s,ink); }
static void keys(render_t *r,const char *p,const char *m,const char *a,const char *b)
{ const char *s[4]={p,m,a,b};for(int i=0;i<4;i++)copy_text(r->labels.key[i],21,s[i]?s[i]:""); }
static bool wrap(render_t *r,int x,int y,int width,const char *p,int rows)
{
    int cols=width/16;if(cols>55)cols=55;if(cols<1)return *p!=0;
    while(*p && rows--) {
        char line[56];int n=0;while(p[n] && p[n]!='\n' && n<cols)n++;
        if(n==cols && p[n] && p[n]!='\n' && p[n]!=' ') {int end=n;while(end>0 && p[end]!=' ')end--;if(end)n=end;}
        for(int i=0;i<n;i++)line[i]=p[i];
        line[n]=0;rtext(r,x,y,2,line,true);y+=22;
        p+=n;if(*p=='\n' || *p==' ')p++;
    }
    return *p!=0;
}
static void unit(fmt_t *f,uint64_t n,const char *s) { number(f,n,0);add(f," ");add(f,s);if(n!=1)add(f,"s"); }
static void age_label(const message_t *m,uint64_t now,char out[64])
{
    copy_text(out,64,"(time unknown)");if(!m->clock)return;
    if(!now){copy_text(out,64,"(age unavailable)");return;}
    if(m->seconds>now){copy_text(out,64,"(future phone time)");return;}
    uint64_t minutes=divide(now-m->seconds,60,0);
    if(!minutes){copy_text(out,64,"(just now)");return;}
    fmt_t f=format(out,64);add(&f,"(");
    if(minutes<60)unit(&f,minutes,"minute");
    else {
        bool hours=minutes<1440;uint32_t rem;
        uint64_t main=divide(minutes,hours?60:1440,&rem);uint64_t rest=hours?rem:rem/60;
        unit(&f,main,hours?"hour":"day");if(rest){add(&f," ");unit(&f,rest,hours?"minute":"hour");}
    }
    add(&f," ago)");
}
static bool leap(unsigned y) { return !(y%4) && (y%100 || !(y%400)); }
static void sent_label(const message_t *m,char out[32])
{
    copy_text(out,32,"time unknown");if(!m->clock)return;
    // Original store only admits UTC from 2020-01-01 through 2100-01-01.
    uint32_t day_seconds;uint64_t days=divide(m->seconds,86400,&day_seconds);unsigned year=1970,month=1;
    while(days>=(unsigned)(365+leap(year))){days-=365+leap(year);year++;}
    static const unsigned md[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    while(days>=md[month-1]+(month==2 && leap(year))){days-=md[month-1]+(month==2 && leap(year));month++;}
    fmt_t f=format(out,32);number(&f,year,4);add(&f,"-");number(&f,month,2);add(&f,"-");number(&f,days+1,2);
    add(&f," ");number(&f,day_seconds/3600,2);add(&f,":");number(&f,day_seconds/60%60,2);add(&f,"Z phone");
}
static void card(render_t *r,int x,int y,int width,int height,const message_t *m,bool selected,bool unread,bool details,uint64_t now)
{
    routline(r,x,y,width,height,selected?3:1);char label[64];fmt_t f=format(label,sizeof label);
    if(unread)add(&f,"* ");
    add(&f,m->name[0]?m->name:"Unnamed");rtext(r,x+16,y+12,3,label,true);
    bool more=wrap(r,x+16,y+48,width-32,m->body,details?5:2);
    if(details && more)rtext(r,x+16,y+height-48,1,"More on phone",true);
    age_label(m,now,label);rtext(r,x+16,y+height-(details?34:20),1,label,true);
    if(details){
        char sent[32];sent_label(m,sent);f=format(label,sizeof label);add(&f,"Badge ");
        static const char hex[]="0123456789abcdef";
        for(int i=0;i<6;i++){char s[3]={hex[m->sender[i]>>4],hex[m->sender[i]&15],0};add(&f,s);}
        add(&f," / ");add(&f,sent);rtext(r,x+16,y+height-20,1,label,true);
    }
}
static void qr(render_t *r,const char *payload)
{
    uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(10)],temp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    if(!qrcodegen_encodeText(payload,temp,code,qrcodegen_Ecc_MEDIUM,1,10,qrcodegen_Mask_AUTO,true))return;
    int n=qrcodegen_getSize(code),scale=240/(n+8),left=W/2+(W/2-n*scale)/2,top=TOP+84+(240-n*scale)/2;
    for(int y=0;y<n;y++)for(int x=0;x<n;x++)if(qrcodegen_getModule(code,x,y))rbox(r,left+x*scale,top+y*scale,scale,scale,true);
}
static void connection(render_t *r,const msg_view_t *v)
{
    int x=W/2+20;rbox(r,W/2-1,TOP,2,H-TOP,true);
    rtext(r,x,TOP+12,3,v->phone_ok?"Connect phone":"Phone unavailable",true);
    if(!v->phone_ok){wrap(r,x,TOP+100,440,"Hold both top buttons for Home, then try again. Messages are kept.",4);return;}
    rtext(r,x,TOP+52,2,r->phone->ssid,true);char payload[96];fmt_t f=format(payload,sizeof payload);
    add(&f,"WIFI:T:WPA;S:");add(&f,r->phone->ssid);add(&f,";P:");add(&f,r->phone->password);add(&f,";;");qr(r,payload);
    rtext(r,x,TOP+338,2,"Password",true);rtext(r,x,TOP+364,2,r->phone->password,true);
    rtext(r,x,TOP+412,2,"http://192.168.4.1",true);rtext(r,x,TOP+446,2,"Keep Wi-Fi, no internet.",true);
    rtext(r,x,TOP+480,2,v->opened?"Reopen page: this reply.":"Reopen page: new message.",true);
    rtext(r,x,TOP+508,2,"Keep drafts before reload.",true);rtext(r,x,TOP+556,2,"Home ends phone mode.",true);
}
bool msg_render_valid(const badge_native_api_t *a,const message_state_t *s,const msg_view_t *v,bool available,const msg_phone_view_t *phone,const char *status,msg_labels_t *labels)
{
    if(!a || !s || !v || !labels || a->abi_version!=2 || a->struct_size<sizeof *a || a->width!=W || a->height!=H || a->top!=TOP ||
       !a->begin_frame || !a->submit_frame || !a->clear || !a->box || !a->outline || !a->text || !a->frame_idle ||
       s->count>MSG_CAP || v->index<0 || v->index>=(s->count?s->count:1) || v->offset<0 || v->offset>=MSG_CAP)return false;
    if(!available && !terminated(status,256))return false;
    if(v->phone_menu && v->phone_ok && (!phone || !terminated(phone->ssid,sizeof phone->ssid) || !terminated(phone->password,sizeof phone->password)))return false;
    bool thread=false;
    for(int i=0;i<s->count;i++){
        const message_t *m=s->messages+i;
        if(!terminated(m->name,sizeof m->name) || !terminated(m->body,sizeof m->body) || m->clock>1 ||
           (m->clock && (m->seconds<1577836800 || m->seconds>UINT64_C(4102444800))))return false;
        if(equal16(v->active,m->thread))thread=true;
    }
    return !v->opened || thread;
}
void msg_visible_ages(const message_state_t *s,const msg_view_t *v,bool available,uint64_t now,char out[3][64])
{
    for(int i=0;i<3;i++)for(int j=0;j<64;j++)out[i][j]=0;
    if(!available)return;
    int indices[3],n=msg_view_visible(s,v,indices);
    for(int i=0;i<n;i++)age_label(s->messages+indices[i],now,out[i]);
}
void msg_render_owned(const badge_native_api_t *a,const message_state_t *s,const msg_view_t *v,bool available,int read_result,uint64_t now,const msg_phone_view_t *phone,const char *status,msg_labels_t *labels)
{
    render_t ctx={.api=a,.phone=phone,.status=status,.labels={{{0}}}},*r=&ctx;
    int width=v->phone_menu?W/2:W,visible[3],nvisible=msg_view_visible(s,v,visible);a->clear(a->context);
    rtext(r,20,TOP+12,3,"Messages",true);char label[32];fmt_t f=format(label,sizeof label);
    if(available){number(&f,s->count,0);add(&f,"/8");add(&f,s->count==MSG_CAP?" FULL":" saved");}else add(&f,"Unavailable");
    rtext(r,width-20-(int)length(label)*16,TOP+16,2,label,true);
    if(!available){
        keys(r,"Info",0,0,v->phone_menu?"Close menu":v->phone_attempted?"Phone (send message)":"Back");
        rtext(r,20,TOP+88,2,"Storage unavailable",true);wrap(r,20,TOP+138,width-40,r->status,5);
        wrap(r,20,TOP+286,width-40,"Existing messages have not been erased. Hold top buttons for Home.",4);
    }else if(!v->opened){
        keys(r,"Up",s->count?"Open":0,"Down",v->phone_menu?"Close menu":"Phone (send message)");
        if(s->count){
            f=format(label,sizeof label);number(&f,v->index+1,0);add(&f," of ");number(&f,s->count,0);add(&f," / unverified");rtext(r,20,TOP+46,2,label,true);
            for(int i=0;i<nvisible;i++)card(r,20,TOP+82+i*138,width-40,126,&s->messages[visible[i]],s->count-1-visible[i]==v->index,!s->read[visible[i]],false,now);
        }else{rtext(r,20,TOP+112,3,"No messages yet",true);wrap(r,20,TOP+172,width-40,"Press Phone (send message) to write a message.",2);}
    }else{
        keys(r,"Older","Reply","Latest",v->phone_menu?"Close menu":"Back");int count=0;
        for(int i=0;i<s->count;i++)if(equal16(v->active,s->messages[i].thread))count++;
        f=format(label,sizeof label);number(&f,count,0);add(&f," messages / unverified");rtext(r,20,TOP+46,2,label,true);
        for(int i=0;i<nvisible;i++)card(r,20,TOP+82+i*220,width-40,210,&s->messages[visible[i]],false,false,true,now);
    }
    if(available){
        rtext(r,20,H-100,2,s->count==MSG_CAP?"Full. Delete on phone.":"Public messages",true);
        rtext(r,20,H-72,2,read_result>MSG_DUP?"Read not saved; use Home":v->phone_attempted?v->phone_ok?"Phone on; sharing paused":"Phone failed; use Home":s->relay?"Nearby sharing ON":"Nearby sharing OFF",true);
        rtext(r,20,H-36,2,"Hold top buttons: Home",true);
    }
    if(v->phone_menu)connection(r,v);
    *labels=r->labels;a->submit_frame(a->context);a->frame_idle(a->context);
}
int msg_render(const badge_native_api_t *a,const message_state_t *s,const msg_view_t *v,bool available,int read_result,uint64_t now,const msg_phone_view_t *phone,const char *status,msg_labels_t *labels)
{
    if(!msg_render_valid(a,s,v,available,phone,status,labels))return MSG_RENDER_INVALID;
    if(!a->begin_frame(a->context))return MSG_RENDER_BUSY;
    msg_render_owned(a,s,v,available,read_result,now,phone,status,labels);return MSG_RENDER_OK;
}
