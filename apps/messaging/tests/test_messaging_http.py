#!/usr/bin/env python3
"""Exercise actual HTTP parsing/escaping with ASan/UBSan; HTTP transport is mocked."""
from pathlib import Path
import subprocess,tempfile,json
root=Path(__file__).resolve().parents[3];s=(root/'apps/messaging/message_portal.c').read_text();out=Path(tempfile.mkdtemp(prefix='badge-msg-http-'))
def fn(marker):
 a=s.index(marker);a=s.rfind('\n',0,a)+1;b=s.index('{',a);depth=1;i=b+1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]+'\n'
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "messaging.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define HTTPD_403_FORBIDDEN 403
#define HTTPD_400_BAD_REQUEST 400
static char key[33]="0123456789abcdef0123456789abcdef";
typedef struct {int content_len,pos,status;const char *body,*token,*phone_time;char output[8192];size_t length;} httpd_req_t;
static int selected_lock=1,lock_depth;
static uint64_t phone_epoch;
static int64_t phone_at;
static time_t wall_now;
static time_t fake_time(time_t *out){if(out)*out=wall_now;return wall_now;}
#define time fake_time
static void xSemaphoreTake(int lock,int wait){assert(lock&&lock_depth++==0);}
static void xSemaphoreGive(int lock){assert(lock&&--lock_depth==0);}
#define portMAX_DELAY -1
static int submits;static uint64_t clock_us;static bool hold_clock,storage_ok=true;static message_state_t store;
static uint64_t esp_timer_get_time(void){if(!hold_clock)clock_us+=1100000;return clock_us;}
static const char *header(httpd_req_t *r,const char *name){return !strcmp(name,"X-Msg-Key")?r->token:r->phone_time?r->phone_time:"";}
static int httpd_req_get_hdr_value_len(httpd_req_t *r,const char *name){return strlen(header(r,name));}
static int httpd_req_get_hdr_value_str(httpd_req_t *r,const char *name,char *out,size_t n){const char *value=header(r,name);if(strlen(value)>=n)return -1;strcpy(out,value);return 0;}
static int httpd_req_recv(httpd_req_t *r,char *out,int n){if(r->pos+n>r->content_len)return -1;memcpy(out,r->body+r->pos,n);r->pos+=n;return n;}
static int httpd_resp_set_status(httpd_req_t *r,const char *status){r->status=atoi(status);return 0;}
static int httpd_resp_set_type(httpd_req_t *r,const char *type){return 0;}
static int httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){return 0;}
static int httpd_resp_send(httpd_req_t *r,const char *data,int n){assert(n>=0&&n<(int)sizeof r->output);memcpy(r->output,data,n);r->output[n]=0;r->length=n;return 0;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *data){return httpd_resp_send(r,data,strlen(data));}
static int httpd_resp_send_err(httpd_req_t *r,int code,const char *body){r->status=code;return httpd_resp_sendstr(r,body);}
void ranging_me(uint8_t out[6]){memset(out,2,6);}
static void select_thread(const uint8_t id[16]){}
void message_portal_selected(uint8_t out[16]){memset(out,0,16);out[0]=1;}
bool messaging_snapshot(message_state_t *out,uint64_t *revision){*out=store;return storage_ok;}
const char *messaging_status(void){return "Storage unavailable; preserved.";}
int messaging_submit(const uint8_t id[16],const uint8_t thread[16],const char *title,const char *body,uint64_t seconds){
 submits++;if(strlen(title)>MSG_TITLE||strlen(body)>MSG_TEXT)return MSG_INVALID;
 message_t m={.logical=1,.seconds=seconds,.clock=seconds?1:0};memcpy(m.id,id,16);memcpy(m.thread,thread,16);m.sender[0]=1;strcpy(m.title,title);strcpy(m.body,body);return message_valid(&m)?MSG_OK:MSG_INVALID;
}
int messaging_read(const uint8_t id[16]){return MSG_DUP;}int messaging_forget(const uint8_t id[16]){return MSG_OK;}int messaging_relay(bool value){return MSG_OK;}
'''
for marker in ['static esp_err_t error(', 'static bool authorized(', 'uint64_t message_portal_now(', 'static void accept_phone_time(', 'static void js(', 'static esp_err_t state(', 'static esp_err_t action(']:code+=fn(marker)
code+=r'''
static int perform(const char *body,int n,const char *token){httpd_req_t r={.body=body,.content_len=n,.token=token,.status=200};action(&r);return r.status;}
static int fields(char *out,const char **list,int n){int total=0;for(int i=0;i<n;i++){size_t k=strlen(list[i])+1;assert(total+k<=512);memcpy(out+total,list[i],k);total+=k;}return total;}
int main(void){
 hold_clock=true;clock_us=1000000;wall_now=0;
 assert(message_portal_now()==0);
 httpd_req_t t={.token="bad",.phone_time="1794157500",.status=200};state(&t);assert(t.status==403&&!phone_epoch);
 t=(httpd_req_t){.token=key,.phone_time="1794157500",.status=200};state(&t);
 assert(t.status==200&&phone_epoch==1794157500ULL&&message_portal_now()==1794157500ULL);
 clock_us+=3900000000ULL;assert(message_portal_now()==1794161400ULL); // 65 monotonic minutes, not a stored send time
 const char *invalid[]={NULL,"","0","-1794157500","+1794157500","1794157500x","17941575000","4102444801","1577836799","179415 500"};
 for(size_t i=0;i<sizeof invalid/sizeof *invalid;i++){uint64_t old=phone_epoch;int64_t at=phone_at;t=(httpd_req_t){.token=key,.phone_time=invalid[i],.status=200};state(&t);assert(t.status==200&&phone_epoch==old&&phone_at==at);}
 wall_now=1794200000;assert(message_portal_now()==1794200000ULL); // valid existing badge clock takes priority
 wall_now=0;clock_us=phone_at-1;assert(message_portal_now()==0); // backwards/reset monotonic cannot invent age
 phone_epoch=0;phone_at=0;assert(message_portal_now()==0); // reboot without clock/reference
 t=(httpd_req_t){.token=key,.phone_time="4102444800",.status=200};state(&t);assert(message_portal_now()==4102444800ULL);clock_us+=1000000;assert(message_portal_now()==0);
 selected_lock=0;assert(message_portal_now()==0);selected_lock=1;
 hold_clock=false;
 fprintf(stderr,"PASS actual clock: authenticated bounded header, badge priority, monotonic advance/reset, invalid retention and reboot\n");
 char body[513],id[33];uint8_t bytes[16]={1};message_hex(bytes,id);const char *parts[]={"send",id,id,"<subject>","<script> & \"hello\"","0"};int n=fields(body,parts,6);
 assert(perform(body,n,"bad")==403&&submits==0);assert(perform(body,n,key)==200&&submits==1);
 assert(perform(body,n-1,key)==400);assert(perform(body,fields(body,parts,5),key)==400);
 n=fields(body,parts,6);body[n++]=0;assert(perform(body,n,key)==400);
 memset(body,'x',512);assert(perform(body,512,key)==400);assert(perform(body,513,key)==400);
 unsigned random=123;for(int i=0;i<3000;i++){int len=i%513;for(int j=0;j<len;j++){random=random*1664525u+1013904223u;body[j]=random>>24;}perform(body,len,key);}
 n=fields(body,parts,6);clock_us+=2000000;hold_clock=true;assert(perform(body,n,key)==200);assert(perform(body,n,key)==200);assert(perform(body,n,key)==429);hold_clock=false;
 for(int i=0;i<MSG_CAP;i++){message_t *m=&store.messages[i];m->id[0]=i+1;m->thread[0]=1;m->sender[0]=2;m->logical=i+1;m->clock=1;m->seconds=4102444800ULL;memset(m->name,'"',12);memset(m->title,'\\',32);memset(m->body,'"',240);}
 store.count=MSG_CAP;httpd_req_t r={.token=key,.status=200};assert(state(&r)==0&&r.status==200&&r.length<8192);puts(r.output);
 storage_ok=false;assert(state(&r)==0&&r.status==503);
}
'''
p=out/'http.c';p.write_text(code)
subprocess.run(['cc','-std=gnu11','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I',str(root/'components/os/messages'),'-I',str(root/'components/os/include'),'-I',str(root/'apps/messaging'),str(p),str(root/'components/os/messages/message_store.c'),'-o',str(out/'http')],check=True)
result=subprocess.check_output([str(out/'http')],text=True);data=json.loads(result)
assert data['limit']==8 and len(data['messages'])==8
assert all(m['body']=='"'*240 and m['title']=='\\'*32 for m in data['messages'])
print('PASS actual HTTP handlers: token, exact NUL fields, truncation/oversize, 3,000 malformed bodies, rate cap, worst-case escaped 8-message JSON, storage unavailable')
print('Maximum tested JSON:',len(result.strip()),'bytes; HTTP harness:',out)
