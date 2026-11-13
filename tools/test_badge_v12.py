#!/usr/bin/env python3
"""v1.2 component Badge/Settings host checks (cc, Node, Git; no device/IDF build).
Actual app renderer/handlers and upstream v1.2 legacy paths run with RAM NVS/image/HTTP stubs.
"""
from pathlib import Path
import subprocess,tempfile,json

C_PRE=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "badge/badge.h"
uint8_t fb[PLANE];
struct entry { char key[32]; char val[4097]; bool present; } store[16];
static bool image, fail_store, fail_image;
static int writes, image_writes;
int cfg_get(const char *key, char *out, int n) {
    *out=0;
    for (int i=0;i<16;i++) if(store[i].present&&!strcmp(key,store[i].key)) {
        int len=strlen(store[i].val); if(len>=n)return -1;
        memcpy(out,store[i].val,len+1); return len;
    }
    return -1;
}
bool cfg_set(const char *key,const char *val) {
    if(fail_store)return false;
    writes++;
    int i; for(i=0;i<16;i++)if(!strcmp(key,store[i].key))break;
    if(i==16)for(i=0;i<16&&*store[i].key;i++);
    assert(i<16); strcpy(store[i].key,key);store[i].present=val!=NULL;
    if(val){assert(strlen(val)<sizeof store[i].val);strcpy(store[i].val,val);}return true;
}
bool cfg_get_image(uint8_t *out) {if(image)memset(out,0xa5,IMG_BYTES);return image;}
bool cfg_set_image(const uint8_t *in) {if(fail_image)return false;image_writes++;image=in!=NULL;return true;}
void icon_base(int x,int y) {(void)x;(void)y;}
void badge_id(char out[5]) {strcpy(out,"0000");}
void box(int x,int y,int w,int h,bool ink) {
    for(int Y=y;Y<y+h;Y++)for(int X=x;X<x+w;X++) if(Y>=TOP&&Y<H&&X>=0&&X<W) {
        if(ink)fb[Y*(W/8)+X/8]|=128>>(X&7); else fb[Y*(W/8)+X/8]&=~(128>>(X&7));
    }
}
static const char *labels[4];
static int button_keys[3], button_count, button_pos, infos, frames, wipes;
void ui_keys(const char*a,const char*b,const char*c,const char*d){labels[0]=a;labels[1]=b;labels[2]=c;labels[3]=d;}
void ui_info(void){infos++;}
void ui_wipe(void){wipes++;} void ui_frame(int a){frames++;} void ui_close(void){}
int btn_get(int a){return button_pos<button_count?button_keys[button_pos++]:BTN_B;}
#include "badge_port.c"
#define px legacy_px
#define big legacy_big
#define centred legacy_centred
#define draw legacy_draw
#define run legacy_run
#define icon legacy_icon
#define app_badge legacy_app_badge
#define app_badge_info legacy_app_badge_info
#include "legacy_badge.c"
#undef px
#undef big
#undef centred
#undef draw
#undef app_badge
#undef app_badge_info
#undef run
#undef icon
typedef int esp_err_t;
typedef struct {int content_len,offset; const char *body;} httpd_req_t;
static char response[100000]; static int response_len,status;
#define HTTPD_400_BAD_REQUEST 400
#define MALLOC_CAP_SPIRAM 0
#define ESP_OK 0
#define LEGACY_DISPLAY_MAX 255
#define TAG "settings"
#define ESP_LOGI(...) ((void)0)
static void *heap_caps_malloc(size_t n,int cap){return malloc(n);}
static int httpd_req_recv(httpd_req_t*r,char*b,int n){int k=r->content_len-r->offset;if(k>n)k=n;memcpy(b,r->body+r->offset,k);r->offset+=k;return k;}
static int httpd_resp_send(httpd_req_t*r,const char*b,int n){assert(n<sizeof response);memcpy(response,b,n);response[n]=0;response_len=n;status=200;return 0;}
static int httpd_resp_sendstr(httpd_req_t*r,const char*b){return httpd_resp_send(r,b,strlen(b));}
static int httpd_resp_send_err(httpd_req_t*r,int code,const char*b){httpd_resp_sendstr(r,b);status=code;return -1;}
static int httpd_resp_send_500(httpd_req_t*r){return httpd_resp_send_err(r,500,"error");}
static void httpd_resp_set_type(httpd_req_t*r,const char*t){}
'''

C_TEST=r'''
static void reset(void){memset(store,0,sizeof store);image=fail_store=fail_image=saved=false;writes=image_writes=0;}
static int save_raw(const char *data,int n,bool old){httpd_req_t r={.content_len=n,.body=data};saved=false;if(old)legacy_post_save(&r);else post_save(&r);return status;}
static int save_pair(const char *s,const char*b){char buf[4096];int n=0;const char*v[]={"badge_subject",s,"badge_body",b};for(int i=0;i<4;i++){int k=strlen(v[i])+1;memcpy(buf+n,v[i],k);n+=k;}return save_raw(buf,n,false);}
static void output(const char*name,const void *b,int n){char path[1024];snprintf(path,sizeof path,"%s/%s",OUTPUT_DIR,name);FILE*f=fopen(path,"wb");assert(f);assert(fwrite(b,1,n,f)==n);fclose(f);}
static void value_is(const char *key,const char *expected){char v[4097];int n=cfg_get(key,v,sizeof v);assert((expected&&n==(int)strlen(expected)&&!strcmp(v,expected))||(!expected&&n==-1));}
int main(int argc,char **argv){
    if(argc==7&&!strcmp(argv[1],"render")) {
        cfg_set("badge_subject",argv[2]);cfg_set("badge_body",argv[3]);cfg_set("badge_text",argv[4]);image=atoi(argv[5]);draw();
        for(int i=0;i<TOP*W/8;i++)assert(fb[i]==0);
        output(argv[6],fb+TOP*W/8,IMG_BYTES);return 0;
    }
    httpd_req_t req={0};char v[1024];
    reset();image=true;cfg_set("badge_text","Kept text");cfg_set("cb_url","http://example.test/feed.txt");cfg_set("tz","CUSTOM-3");cfg_set("snd_sao","2");
    writes=image_writes=0;get_cfg(&req);output("upstream-settings.json",response,response_len);assert(!writes&&!image_writes);
    const char upstream_edit[]="cb_url\0http://example.test/new.txt\0tz\0UTC0\0snd_sao\0" "3\0";
    assert(save_raw(upstream_edit,sizeof upstream_edit-1,false)==200&&image&&!image_writes);
    value_is("cb_url","http://example.test/new.txt");value_is("tz","UTC0");value_is("snd_sao","3");value_is("badge_text","Kept text");
    writes=0;const char bad_url[]="cb_url\0https://example.test/\0";assert(save_raw(bad_url,sizeof bad_url-1,false)==400&&!writes);
    const char bad_port[]="snd_sao\0" "4\0";assert(save_raw(bad_port,sizeof bad_port-1,false)==400&&!writes);
    puts("PASS: v1.2 cb_url/tz/snd_sao read/save/validation, untouched Badge picture/text and unknown custom timezone preserved");

    reset();draw();uint8_t defaults[PLANE];memcpy(defaults,fb,PLANE);
    // Existing legacy NVS and picture survive reads, preview requests and unrelated saves, byte for byte.
    const char *fixture[][2]={{"name","OLDNAME"},{"ssid","old-network"},{"pass","old-password"},
        {"wx_city","Pasadena"},{"wx_lat","34.14"},{"wx_lon","-118.14"},{"wx_unit","F"},{"wx_place","Pasadena, US"},
        {"badge_text","Existing text\r\nwith punctuation: <>&\\\""}};
    for(int i=0;i<sizeof fixture/sizeof *fixture;i++)cfg_set(fixture[i][0],fixture[i][1]);
    image=true;writes=image_writes=0;draw();assert(writes==0&&image_writes==0);
    for(int i=TOP*W/8;i<PLANE;i++)assert(fb[i]==0xa5);
    get_cfg(&req);assert(writes==0&&image_writes==0);output("upgrade.json",response,response_len);
    for(int i=0;i<sizeof fixture/sizeof *fixture;i++)value_is(fixture[i][0],fixture[i][1]);
    value_is("badge_subject",NULL);value_is("badge_body",NULL);value_is("badge_legacy",NULL);
    const char unrelated[]="name\0NEXT\0";assert(save_raw(unrelated,sizeof unrelated-1,false)==200&&image&&image_writes==0);
    value_is("badge_text",fixture[8][1]);value_is("badge_subject",NULL);
    image=false;draw();assert(memcmp(fb,defaults,PLANE));assert(writes==1); // only the explicit username edit
    // New text has a readable legacy mirror, while the split representation and unrelated values remain intact.
    assert(save_pair("HELLO","Meet me at the workshop.")==200&&saved&&!image&&image_writes==1);
    value_is("badge_subject","HELLO");value_is("badge_body","Meet me at the workshop.");
    value_is("badge_text","HELLO\nMeet me at the workshop.");value_is("badge_legacy","HELLO\nMeet me at the workshop.");
    value_is("ssid","old-network");get_cfg(&req);output("roundtrip.json",response,response_len);
    assert(cfg_get("badge_text",v,256)>0);legacy_draw();assert(memcmp(fb,defaults,PLANE));
    // Integrated PR01 controls keep the new layout and stored fields across Info, Redraw and Home.
    draw();uint8_t split[PLANE];memcpy(split,fb,PLANE);
    button_keys[0]=BTN_PLUS;button_keys[1]=BTN_A;button_keys[2]=BTN_B;button_count=3;button_pos=infos=frames=wipes=0;
    writes=image_writes=0;app_badge.run();
    assert(infos==1&&frames==2&&wipes==2&&!memcmp(split,fb,PLANE)&&writes==0&&image_writes==0);
    assert(!strcmp(labels[0],"Info")&&!strcmp(labels[2],"Redraw")&&!strcmp(labels[3],"Home"));
    value_is("badge_subject","HELLO");value_is("badge_body","Meet me at the workshop.");
    // Downgrade, edit using the real legacy handler, then re-upgrade: the old edit wins without erasing split data.
    const char old_edit[]="badge_text\0Changed by legacy\nKeep this\0";assert(save_raw(old_edit,sizeof old_edit-1,true)==200);
    writes=image_writes=0;draw();uint8_t edited[PLANE];memcpy(edited,fb,PLANE);get_cfg(&req);output("old-edit.json",response,response_len);
    assert(writes==0&&image_writes==0);value_is("badge_subject","HELLO");value_is("badge_body","Meet me at the workshop.");
    cfg_set("badge_subject",NULL);cfg_set("badge_body",NULL);cfg_set("badge_legacy",NULL);draw();assert(!memcmp(fb,edited,PLANE));
    // Old firmware clearing text is also detected; an old picture continues to have precedence.
    assert(save_pair("SUBJECT","BODY")==200);const char old_clear[]="badge_text\0\0";assert(save_raw(old_clear,sizeof old_clear-1,true)==200);
    writes=image_writes=0;draw();assert(!memcmp(fb,defaults,PLANE)&&writes==0&&image_writes==0);value_is("badge_body","BODY");
    image=true;draw();for(int i=TOP*W/8;i<PLANE;i++)assert(fb[i]==0xa5);legacy_draw();for(int i=TOP*W/8;i<PLANE;i++)assert(fb[i]==0xa5);
    // Explicitly clearing the new fields clears both views, so neither version resurrects an old message.
    assert(save_pair("","")==200);draw();assert(!memcmp(fb,defaults,PLANE));legacy_draw();assert(!memcmp(fb,defaults,PLANE));
    // Preserve all 1000 supported legacy bytes. legacy's renderer limit is inherited, not a storage migration.
    reset();char s[258],b[1002];memset(s,'S',256);s[256]=0;memset(b,'B',1000);b[1000]=0;
    cfg_set("badge_text",b);writes=0;draw();assert(memcmp(fb,defaults,PLANE)&&writes==0);value_is("badge_text",b);
    assert(save_pair(s,b)==200);value_is("badge_subject",s);value_is("badge_body",b);value_is("badge_text",b);value_is("badge_legacy",b);
    assert(cfg_get("badge_text",v,sizeof v)==1000);assert(cfg_get("badge_text",v,256)==-1); // unchanged legacy 255-character display cap
    draw();assert(memcmp(fb,defaults,PLANE));
    writes=0;s[256]='S';s[257]=0;assert(save_pair(s,b)==400&&!saved&&writes==0);s[256]=0;
    b[1000]='B';b[1001]=0;assert(save_pair(s,b)==400&&writes==0);b[1000]=0;
    assert(save_pair("caf\xc3\xa9","normal")==400&&writes==0);
    assert(save_pair("valid\nsubject","line1\r\nline2\tend")==200);value_is("badge_body","line1\r\nline2\tend");
    // A title must not make an otherwise legacy-readable body exceed its 255-character renderer.
    memset(s,'S',50);s[50]=0;memset(b,'B',220);b[220]=0;assert(save_pair(s,b)==200);value_is("badge_text",b);assert(cfg_get("badge_text",v,256)==220);
    // The legacy endpoint stays usable and explicitly replaces the split fields.
    reset();assert(save_pair("NEW","BODY")==200);const char legacy[]="badge_text\0legacy\0";assert(save_raw(legacy,sizeof legacy-1,false)==200);
    value_is("badge_text","legacy");value_is("badge_subject",NULL);value_is("badge_body",NULL);value_is("badge_legacy",NULL);
    writes=0;const char missing[]="badge_subject\0hello\0";assert(save_raw(missing,sizeof missing-1,false)==400&&writes==0);
    const char malformed[]="badge_subject\0hello";assert(save_raw(malformed,sizeof malformed-1,false)==400&&writes==0);
    char too_many[100];memset(too_many,0,sizeof too_many);assert(save_raw(too_many,sizeof too_many,false)==400&&writes==0);
    reset();fail_store=true;assert(save_pair("hello","body")==500&&!saved);fail_store=false;fail_image=true;assert(save_pair("hello","body")==500&&!saved);
    reset();char worst[1024];memset(worst,1,1023);worst[1023]=0;for(int i=0;i<sizeof keys/sizeof *keys;i++)cfg_set(keys[i],worst);get_cfg(&req);assert(response_len<16*1024+IMG_BYTES);
    get_page(&req);output("index.html",response,response_len);get_font(&req);output("badge-font",response,response_len);
    puts("PASS: integrated Info/Redraw/Home; legacy upgrade/read/no-migration, downgrade mirror, old edits/clear after re-upgrade, saved images, unrelated settings, full bounds, malformed requests, save failures and JSON bounds");
    puts("LIMIT: original legacy renderer reads at most 255 characters; its portal still reads the full <=1000-character compatibility view");
}
'''

PREVIEW_TEST=r'''const fs=require('node:fs'),vm=require('node:vm'),cp=require('node:child_process'),assert=require('node:assert');
const dir=process.argv[2]+'/',html=fs.readFileSync(dir+'index.html','utf8');
const nodes={};for(const m of html.matchAll(/id=(?:"([^"]+)"|'([^']+)'|([^\s>]+))/g))nodes[m[1]||m[2]||m[3]]={value:'',checked:false,hidden:false,textContent:''};
nodes.mt.checked=nodes.wc.checked=true;
let frame,requests=[];
const ctx={createImageData:(w,h)=>({data:new Uint8ClampedArray(w*h*4)}),putImageData:o=>{frame=o.data}};
nodes.pv.getContext=nodes.cv.getContext=()=>ctx;
const font=fs.readFileSync(dir+'badge-font');
nodes.networks.options=[];nodes.networks.add=function(o){this.options.push(o)};
nodes.tz.options=[];nodes.tz.add=function(o){this.options.push(o)};
const sandbox={TextEncoder,AbortController,setTimeout,clearTimeout,Option:function(text,value){this.text=text;this.value=value},document:{getElementById:id=>{assert(nodes[id],id);return nodes[id]}},fetch:async(url,args)=>{
  requests.push({url,args});if(url.startsWith('/networks'))return {ok:true,json:async()=>({networks:[],available:false,busy:false})};return {ok:true,json:async()=>({img:false,id:'0000',badge_text:'Existing legacy text',tz:'CUSTOM-3',cb_url:'http://example.test/feed.txt',snd_sao:'2'}),arrayBuffer:async()=>font.buffer.slice(font.byteOffset,font.byteOffset+font.byteLength),text:async()=>'Saved.'};
},console};
vm.createContext(sandbox);vm.runInContext(html.split('<script>')[1].split('</script>')[0],sandbox);
(async()=>{
nodes.save.onclick();assert(!requests.some(r=>r.url==='/save'));
await new Promise(resolve=>setImmediate(resolve));
assert.equal(nodes.tz.value,'CUSTOM-3');assert(nodes.tz.options.some(o=>o.value==='CUSTOM-3'));assert.equal(nodes.cb_url.value,'http://example.test/feed.txt');assert.equal(nodes.snd_sao.value,'2');
requests=[];nodes.save.onclick();await new Promise(resolve=>setImmediate(resolve));
assert(requests.find(r=>r.url==='/save').args.body.includes('cb_url\0http://example.test/feed.txt\0tz\0CUSTOM-3\0snd_sao\x002\0'));
assert(!requests.find(r=>r.url==='/save').args.body.includes('badge_body'));
const roundtrip=JSON.parse(fs.readFileSync(dir+'roundtrip.json','utf8'));assert(roundtrip.badge_subject==='HELLO'&&roundtrip.badge_body==='Meet me at the workshop.');
const edited=JSON.parse(fs.readFileSync(dir+'old-edit.json','utf8'));assert(!edited.badge_subject&&!edited.badge_body&&edited.badge_text==='Changed by legacy\nKeep this');
const cases=[
['empty','',''],['both-short','HELLO','Meet me at the workshop.'],['subject-only','HELLO',''],['body-only','','Meet me at the workshop.'],
['paragraph','BUILD TOGETHER','Bring your badge to the workshop. We will compare ideas, try the games, and learn how the display works.\nEveryone is welcome.'],
['multiline','FIRST\nSECOND','Line one\n\nLine three\nLine four'],['crlf-tabs','\tHELLO\r\nWORLD  ','line\tone\rline two\r\nline three\n'],
['blank-field','  \n\t','All space belongs to this body.'],['max-both','S'.repeat(256),'B'.repeat(1000)],
['max-body','','The quick brown fox jumps over the lazy dog. '.repeat(24).slice(0,1000)],
['long-words','W'.repeat(51),'supercalifragilisticexpialidocious'.repeat(10)],['many-breaks','TITLE',('x\n').repeat(100)],
['legacy','','','An existing badge message\nwith its line breaks'],['legacy-unicode','','','Café ☀ badge'],['legacy-1000','','','L'.repeat(1000)],
];
let count=0;
for(const [name,s,b,legacy='']of cases){
nodes.badge_subject.value=s;nodes.badge_body.value=b||legacy;sandbox.preview();
assert(frame&&frame.length===960*624*4);
const preview=Buffer.alloc(74880);for(let i=0;i<960*624;i++)if(frame[i*4+1]===0)preview[i>>3]|=128>>(i&7);
cp.execFileSync(dir+'host_test',['render',s,b,legacy,'0',name+'.raw']);
const device=fs.readFileSync(dir+name+'.raw');if(!preview.equals(device)){fs.writeFileSync(dir+name+'.preview.raw',preview);console.log(name,'wrap',sandbox.wrap(sandbox.normalise(b||legacy),28),'diff bytes',preview.filter((v,i)=>v!==device[i]).length);throw Error(name+' pixel mismatch')}
if(name==='many-breaks')assert(nodes.pst.textContent.includes('Too many line breaks'));
count++;
}
cp.execFileSync(dir+'host_test',['render','SUBJECT','BODY','legacy','1','picture.raw']);assert(fs.readFileSync(dir+'picture.raw').every(v=>v===0xa5));
// The actual form must send NUL-delimited fields and preserve supported multiline data.
nodes.badge_subject.value='Round trip';nodes.badge_body.value='Line 1\nLine 2';nodes.badge_body.oninput();requests=[];nodes.save.onclick();await new Promise(resolve=>setImmediate(resolve));
const post=requests.find(r=>r.url==='/save');assert(post&&post.args.body.includes('badge_subject\0Round trip\0badge_body\0Line 1\nLine 2\0'));
nodes.mi.checked=true;sandbox.hadImg=true;requests=[];nodes.save.onclick();await new Promise(resolve=>setImmediate(resolve));
assert(!requests.find(r=>r.url==='/save').args.body.includes('badge_body'));
nodes.mi.checked=false;nodes.badge_body.value='unsupported ☀';requests=[];nodes.save.onclick();assert(requests.length===0&&nodes.st.textContent.includes('plain English'));
console.log('PASS: '+count+' device/preview pixel-identical cases; image precedence; untouched-form preservation, real form NUL payload, picture mode and unsupported-character feedback');
})();
'''

def main():
    repo=Path(__file__).resolve().parents[1]
    out=Path(tempfile.mkdtemp(prefix="badge-v12-"))
    baseline="b43e45087abb0604263d3edc84d0c0fcdd47f6dc"
    old_badge=subprocess.check_output(["git","show",baseline+":apps/badge/badge.c"],cwd=repo,text=True)
    old_badge=old_badge.replace('#include "badge/font8x8_basic.h"','// Font already included.')
    (out/"legacy_badge.c").write_text(old_badge)
    (out/"badge_port.c").write_text((repo/"apps/badge/badge.c").read_text())
    old_settings=subprocess.check_output(["git","show",baseline+":apps/settings/settings.c"],cwd=repo,text=True)
    old_save=old_settings[old_settings.index("static esp_err_t post_save"):old_settings.index("static esp_err_t post_image")].replace("post_save(","legacy_post_save(")
    settings=(repo/"apps/settings/settings.c").read_text()
    handlers=settings[settings.index("static volatile bool saved;"):settings.index("static esp_err_t post_image")]
    page=(repo/"apps/settings/settings_page.h").read_text()
    (out/"host_test.c").write_text(C_PRE+page+handlers.replace('#include "settings_networks.h"','')+old_save+C_TEST)
    subprocess.run(["cc","-std=c11","-O1","-g","-Wall","-Wextra","-Werror","-Wno-unused-parameter","-Wno-sign-compare","-Wno-deprecated-declarations","-fsanitize=address,undefined",
        "-I",str(repo/"components/os/include"),"-I",str(out),'-DOUTPUT_DIR='+json.dumps(str(out)),str(out/"host_test.c"),"-o",str(out/"host_test")],check=True)
    subprocess.run([str(out/"host_test")],check=True)
    (out/"check_preview.cjs").write_text(PREVIEW_TEST)
    subprocess.run(["node",str(out/"check_preview.cjs"),str(out)],check=True)
    print("Evidence:",out)
if __name__=="__main__":main()
