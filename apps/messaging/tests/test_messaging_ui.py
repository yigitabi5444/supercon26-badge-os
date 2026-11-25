#!/usr/bin/env python3
"""Actual native Messages app/descriptor with target drawing code, scripted buttons and real QR. Top bar is checked by the OS owner; no device access."""
from pathlib import Path
import subprocess,tempfile,struct,zlib,binascii,os
root=Path(__file__).resolve().parents[3];out=Path(tempfile.mkdtemp(prefix='badge-msg-split-'))
(out/'esp_err.h').write_text('typedef int esp_err_t;\n#define ESP_OK 0\n')
code=r'''
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *allocated;
static void *app_alloc(size_t n,size_t size) { allocated=calloc(n,size);return allocated; }
#define calloc app_alloc
#include "NATIVE_MESSAGING_SOURCE"
#undef calloc
#include "qrcodegen.h"
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
uint8_t fb[PLANE];
static message_state_t sim;
static uint64_t revision=1,current_now=1794157500;
static bool available=true,portal_started,portal_attempted,portal_fail,divider,inject_after_snapshot;
static void add(int id,int thread);
static int stage,scenario,frames,closes,portal_calls,portal_setups,idle_frames;
static uint8_t context[16];
static const char *labels[4];
static jmp_buf done;
static struct {int x,y,scale;char s[128];} words[160];static int nwords;
'''
code+=r'''
#define box raster_box
#define outline raster_outline
#define text raster_text
#define icon_base raster_icon_base
#include "NATIVE_DRAW_SOURCE"
#undef box
#undef outline
#undef text
#undef icon_base
'''
code+=r'''
void box(int x,int y,int w,int h,bool ink) {
 assert(x>=0&&y>=TOP&&w>=0&&h>=0&&x+w<=W&&y+h<=H);
 if(x==W/2-1&&y==TOP&&w==2)divider=true;
 raster_box(x,y,w,h,ink);
}
void outline(int x,int y,int w,int h,int t) {
 box(x,y,w,t,true);box(x,y+h-t,w,t,true);box(x,y,t,h,true);box(x+w-t,y,t,h,true);
}
void text(int x,int y,int scale,const char *s,bool ink) {
 assert(x>=0&&y>=TOP&&y+8*scale<=H&&x+strlen(s)*8*scale<=W&&nwords<160&&strlen(s)<128);
 words[nwords].x=x;words[nwords].y=y;words[nwords].scale=scale;strcpy(words[nwords++].s,s);
 raster_text(x,y,scale,s,ink);
}
void ui_keys(const char *plus,const char *minus,const char *a,const char *b) {labels[0]=plus;labels[1]=minus;labels[2]=a;labels[3]=b;}
static struct {int x,y,scale;char s[128];} shown[160];static int nshown;
void ui_frame(int n) {
 assert(n==1&&frames==closes);
 for(int i=0;i<nwords;i++)if(divider){int end=words[i].x+strlen(words[i].s)*8*words[i].scale;if(end>(words[i].x<480?460:940))fprintf(stderr,"Overflow x=%d end=%d text=%s\n",words[i].x,end,words[i].s);assert(end<=(words[i].x<480?460:940));}
 memcpy(shown,words,sizeof words);nshown=nwords;frames++;nwords=0;divider=false;
}
void ui_close(void) {assert(frames==closes+1);closes++;}
void ui_info(void) {}
void icon_base(int x,int y) {raster_icon_base(x,y);}
uint64_t message_portal_now(void) {return current_now;}
bool messaging_start(void) {return true;}
bool messaging_snapshot(message_state_t *out,uint64_t *gen) {if(!available)return false;*out=sim;if(gen)*gen=revision;if(inject_after_snapshot){inject_after_snapshot=false;add(2,1);}return true;}
const char *messaging_status(void) {return "Storage write failed. Messages kept.";}
int messaging_read(const uint8_t thread[16]) {int result=MSG_DUP;for(int i=0;i<sim.count;i++)if(!memcmp(thread,sim.messages[i].thread,16)&&!sim.read[i])sim.read[i]=1,result=MSG_OK;if(result==MSG_OK)revision++;return result;}
bool message_portal_start(const uint8_t thread[16]) {
 portal_calls++;
 if(portal_started){if(thread)memcpy(context,thread,16);else memset(context,0,16);return true;}
 if(portal_attempted)return false;portal_attempted=true;portal_setups++;
 if(portal_fail)return false;portal_started=true;if(thread)memcpy(context,thread,16);else memset(context,0,16);return true;
}
const char *message_portal_ssid(void) {return "Msg-1A2B";}
const char *message_portal_password(void) {return "0123456789abcdef";}
void esp_qrcode_print_console(esp_qrcode_handle_t q) {}
int esp_qrcode_get_size(esp_qrcode_handle_t q) {return qrcodegen_getSize(q);}
bool esp_qrcode_get_module(esp_qrcode_handle_t q,int x,int y) {return qrcodegen_getModule(q,x,y);}
int esp_qrcode_generate(esp_qrcode_config_t *c,const char *payload) {
 uint8_t buf[qrcodegen_BUFFER_LEN_MAX],temp[qrcodegen_BUFFER_LEN_MAX];
 assert(qrcodegen_encodeText(payload,temp,buf,(enum qrcodegen_Ecc)c->qrcode_ecc_level,1,c->max_qrcode_version,qrcodegen_Mask_AUTO,true));
 c->display_func(buf);return 0;
}
static bool has(const char *s) {for(int i=0;i<nshown;i++)if(strstr(shown[i].s,s))return true;return false;}
static message_t sample(int id,int thread) {
 message_t m={.logical=id,.clock=1,.seconds=1794157500};m.id[0]=id;m.thread[0]=thread;m.sender[0]=2;m.sender[5]=id;
 const char *names[]={"Guest","Visitor","Maker","Volunteer","Guest","Visitor","Maker","Volunteer"};
 const char *bodies[]={"Meet by the main stage after the talk?","Yes, see you there.","I'll bring the spare cable.","Anyone heading to the workshop?","There's room at the front.","I'm by the entrance.","Coffee after the session?","Can we meet near the desk?"};
 if(id<=4){const int ages[]={7200,3900,300,30};m.seconds-=ages[id-1];}
 snprintf(m.name,sizeof m.name,"%s",names[(id-1)%8]);snprintf(m.title,sizeof m.title,"Conversation %d",thread);snprintf(m.body,sizeof m.body,"%s",bodies[(id-1)%8]);return m;
}
static void add(int id,int thread) {assert(sim.count<MSG_CAP);sim.messages[sim.count]=sample(id,thread);sim.read[sim.count++]=0;revision++;}
static void erase_thread(int thread) {for(int i=0;i<sim.count;)if(sim.messages[i].thread[0]==thread){memmove(sim.messages+i,sim.messages+i+1,(sim.count-i-1)*sizeof sim.messages[0]);memmove(sim.read+i,sim.read+i+1,sim.count-i-1);sim.count--;}else i++;revision++;}
static void dump(const char *name) {char p[512];snprintf(p,sizeof p,"UI_OUTPUT/%s.pbm",name);FILE *f=fopen(p,"wb");assert(f);fprintf(f,"P4\n%d %d\n",W,H);fwrite(fb,1,PLANE,f);fclose(f);}
int btn_get(int timeout) {
 assert(timeout==1000&&frames==closes);
 if(scenario==1){
  switch(stage++) {
   case 0:assert(has("Volunteer")&&has("1 of 4")&&!strcmp(labels[0],"Up")&&!strcmp(labels[1],"Open")&&!strcmp(labels[2],"Down"));return BTN_A;
   case 1:assert(has("2 of 4"));return BTN_A;
   case 2:assert(has("3 of 4"));return BTN_MINUS;
   case 3:assert(has("Yes, see")&&!has("I'll bring"));add(5,1);return -1;
   case 4:assert(has("Yes, see")&&!has("There's room")&&!sim.read[sim.count-1]);return BTN_A;
   case 5:assert(has("There's room")&&sim.read[sim.count-1]&&!strcmp(labels[1],"Reply")&&!strcmp(labels[2],"Latest"));return BTN_MINUS;
   case 6:assert(!strcmp(labels[3],"Close menu")&&context[0]==1&&has("There's room"));dump("reply");add(6,1);return -1;
   case 7:assert(has("I'm by")&&has("Connect phone"));return BTN_B;
   case 8:assert(!has("Connect phone")&&has("Phone on; sharing paused")&&has("I'm by")&&!strcmp(labels[3],"Back"));return BTN_MINUS;
   case 9:assert(portal_setups==1&&has("Connect phone")&&context[0]==1);return BTN_B;
   case 10:return BTN_B;
   case 11:assert(has("5 of 6")&&has("Yes, see"));add(7,7);return -1;
   case 12:assert(has("1 of 7")&&has("Coffee after"));erase_thread(4);return -1;
   case 13:assert(has("1 of 6")&&has("Coffee after"));erase_thread(1);return -1;
   case 14:assert(has("1 of 1")&&has("Coffee after"));idle_frames=frames;return -1;
   case 15:assert(frames==idle_frames&&portal_setups==1);longjmp(done,1);
  }
 }else if(scenario==3||scenario==4){
  switch(stage++){
   case 0:return BTN_MINUS; // explicit Open anchors the currently latest A
   case 1:inject_after_snapshot=true;return BTN_PLUS; // dirty passive redraw, still anchored A
   case 2:assert(!has("Yes, see")&&sim.count==2&&!sim.read[1]);return -1;
   case 3:assert(has("Meet by")&&!has("Yes, see")&&!sim.read[1]);return BTN_A;
   case 4:assert(has("Yes, see")&&sim.read[1]&&portal_setups==0);longjmp(done,1);
  }
 }else if(scenario==5){
  switch(stage++){
   case 0:assert(has("age unavailable"));idle_frames=frames;return -1;
   case 1:assert(frames==idle_frames);current_now=1794157500;return -1;
   case 2:assert(frames==idle_frames+1&&has("just now"));idle_frames=frames;current_now+=59;return -1;
   case 3:assert(frames==idle_frames);current_now++;return -1;
   case 4:assert(frames==idle_frames+1&&has("1 minute ago"));idle_frames=frames;current_now+=59;return -1;
   case 5:assert(frames==idle_frames);current_now++;return -1;
   case 6:assert(frames==idle_frames+1&&has("2 minutes ago"));current_now=0;return -1;
   case 7:assert(has("age unavailable"));current_now=1794157499;return -1;
   case 8:assert(has("future phone time"));longjmp(done,1);
  }
 }else if(scenario==6){
  switch(stage++){
   case 0:return BTN_A;
   case 1:return BTN_A;
   case 2:assert(has("3 of 4"));return BTN_B;
   case 3:assert(has("3 of 4")&&has("Connect phone"));return BTN_B;
   case 4:assert(has("1 of 4")&&has("Volunteer")&&!has("Connect phone"));longjmp(done,1);
  }
 }else{
  switch(stage++){
   case 0:return BTN_B;
   case 1:assert(has("Phone unavailable")&&has("Meet by"));return BTN_B;
   case 2:assert(has("Phone failed; use Home")&&!has("Phone unavailable"));return BTN_B;
   case 3:assert(portal_setups==1&&has("Phone unavailable"));return BTN_B;
   case 4:available=false;return -1;
   case 5:assert(has("Storage unavailable")&&!has(" saved")&&!strcmp(labels[3],"Phone"));return BTN_B;
   case 6:assert(has("Storage unavailable")&&has("Phone unavailable")&&portal_setups==1);longjmp(done,1);
  }
 }
 assert(0);return -1;
}
static void reset(void) {
 memset(&sim,0,sizeof sim);sim.relay=1;revision=1;current_now=1794157500;stage=frames=closes=portal_calls=portal_setups=nwords=nshown=0;
 available=true;portal_started=portal_attempted=portal_fail=divider=inject_after_snapshot=false;memset(context,0,16);
}
int main(void) {
 // Actual formatter: truthful missing/future clocks, boundaries and compact units.
 message_t m=sample(4,4);m.seconds=1794157500;char age[64];
 struct {uint64_t elapsed;const char *label;} ages[]={
  {0,"(just now)"},{59,"(just now)"},{60,"(1 minute ago)"},{300,"(5 minutes ago)"},
  {3600,"(1 hour ago)"},{3900,"(1 hour 5 minutes ago)"},{7260,"(2 hours 1 minute ago)"},
  {86400,"(1 day ago)"},{90000,"(1 day 1 hour ago)"},{180000,"(2 days 2 hours ago)"}};
 for(size_t i=0;i<sizeof ages/sizeof ages[0];i++){age_label(&m,m.seconds+ages[i].elapsed,age);assert(!strcmp(age,ages[i].label));}
 age_label(&m,0,age);assert(!strcmp(age,"(age unavailable)"));
 age_label(&m,m.seconds-1,age);assert(!strcmp(age,"(future phone time)"));
 m.clock=0;age_label(&m,1794157500,age);assert(!strcmp(age,"(time unknown)"));
 reset();scenario=5;add(4,4);sim.messages[0].seconds=1794157500;current_now=0;
 if(!setjmp(done))app_messaging.run();free(allocated);allocated=NULL;
 reset();scenario=6;for(int i=1;i<=4;i++)add(i,i==4?4:1);
 if(!setjmp(done))app_messaging.run();free(allocated);allocated=NULL;
 reset();for(int i=1;i<=4;i++)add(i,i==4?4:1);
 view_t v={.phone_menu=true,.phone_attempted=true,.phone_ok=true};selection(&sim,&v);draw(&sim,&v,true,MSG_DUP,current_now);dump("split");
 v.phone_menu=false;draw(&sim,&v,true,MSG_DUP,current_now);dump("full");
 reset();scenario=1;for(int i=1;i<=4;i++)add(i,i==4?4:1);
 if(!setjmp(done))app_messaging.run();free(allocated);allocated=NULL;
 reset();scenario=2;portal_fail=true;add(1,1);
 if(!setjmp(done))app_messaging.run();free(allocated);allocated=NULL;
 // Insert unread B after a snapshot of anchored A, before the old passive read call.
 for(int test=3;test<=4;test++){
  reset();scenario=test;add(1,1);sim.read[0]=1;
  if(test==4){sim.messages[0].id[0]=0;sim.messages[0].id[15]=1;} // a valid ID may start with zero
  if(!setjmp(done))app_messaging.run();free(allocated);allocated=NULL;
 }
 // Full and unknown-time fixtures exercise the same half-width geometry and feedback.
 reset();for(int i=1;i<=8;i++)add(i,i);sim.messages[7].clock=0;sim.messages[7].seconds=0;
 strcpy(sim.messages[7].name,"LongName1234");memset(sim.messages[7].body,'W',MSG_TEXT);sim.messages[7].body[MSG_TEXT]=0;
 v=(view_t){.phone_menu=true,.phone_attempted=true,.phone_ok=true};selection(&sim,&v);draw(&sim,&v,true,MSG_DUP,current_now);
 assert(has("8/8 FULL")&&has("time unknown")&&has("just now")&&has("Full. Delete on phone."));dump("full-capacity");
 puts("PASS actual native UI: relative ages/unknown/future/minute-only redraw, left-pair controls, newest entry/arrival/Close, card/thread anchor, half/full bounds, full, close/reopen/session context, live send, stable IDs/deletion, no idle redraw, failed-start/storage failure and post-snapshot pinned unread race");
}
'''
p=out/'ui.c';p.write_text(code.replace('UI_OUTPUT',str(out)).replace('NATIVE_MESSAGING_SOURCE',str(root/'apps/messaging/messaging.c')).replace('NATIVE_DRAW_SOURCE',str(root/'components/os/draw.c')))
qr=Path(os.environ.get('MSG_QR_COMPONENT',str(root/'managed_components/espressif__qrcode')))
if not (qr/'qrcodegen.c').is_file(): raise SystemExit('Run the integration dependency setup first, or set MSG_QR_COMPONENT to the installed espressif__qrcode directory.')
subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-fsanitize=address,undefined','-g','-I',str(out),'-I',str(root/'components/os/messages'),'-I',str(root/'components/os/include'),'-I',str(root/'apps/messaging'),'-I',str(qr),'-I',str(qr/'include'),str(p),str(root/'components/os/messages/message_store.c'),str(qr/'qrcodegen.c'),'-o',str(out/'ui')],check=True)
subprocess.run([str(out/'ui')],check=True)
def chunk(kind,body):return struct.pack('>I',len(body))+kind+body+struct.pack('>I',binascii.crc32(kind+body)&0xffffffff)
for p in out.glob('*.pbm'):
 magic,dim,data=p.read_bytes().split(b'\n',2);assert magic==b'P4';w,h=map(int,dim.split())
 rows=b''.join(b'\x00'+bytes(b^255 for b in data[y*w//8:(y+1)*w//8]) for y in range(h))
 p.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,1,0,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
print('Native UI fixtures:',out)
