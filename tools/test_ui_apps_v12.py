#!/usr/bin/env python3
"""Run actual v1.2 app control paths against small host mocks.
Run: python3 tools/test_ui_apps_v12.py. Requires a C compiler with ASan/UBSan.
Tests modal wiring and preserved controls; Badge compatibility is covered separately; network/hardware calls are mocked.
"""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
outdir=Path(tempfile.mkdtemp(prefix='badge-ui-apps-'))
def src(app): return (root/'apps'/app/(app+'.c')).read_text()
def clean(s): return '\n'.join(l for l in s.splitlines() if not l.startswith('#include'))
common=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <setjmp.h>
#include "badge/badge.h"
uint8_t fb[PLANE];
static const char *labels[4];
static int keys[16],key_count,key_pos,infos,frames,wipes,closes,draws,fetches,notifications;
static int app_panel,lock_depth,next_mutex=1,delay_checks;
static int64_t tick_now;
static jmp_buf stop;
static unsigned random_state=17;
static bool info_requires_panel;
static const char *legacy_text;
static bool saved_picture;
static int image_reads,legacy_reads;
typedef int SemaphoreHandle_t;
typedef int TaskHandle_t;
typedef int64_t TickType_t;
#define portMAX_DELAY -1
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define MALLOC_CAP_SPIRAM 0
#define EXT_RAM_BSS_ATTR
#define taskENTER_CRITICAL(x) ((void)0)
#define taskEXIT_CRITICAL(x) ((void)0)
void icon_base(int x,int y) { (void)x; (void)y; }
void ui_keys(const char *a,const char *b,const char *c,const char *d) { labels[0]=a;labels[1]=b;labels[2]=c;labels[3]=d; }
void ui_frame(int n) { frames++; }
void ui_close(void) { closes++; }
void ui_wipe(void) { wipes++; }
void ui_info(void) { if(info_requires_panel) assert(lock_depth==1); infos++; tick_now+=60000; }
void ui_info_until(const volatile bool *dismiss) { infos++; *(volatile bool*)dismiss=true; }
int btn_get(int t) { if(key_pos==key_count) longjmp(stop,1); return keys[key_pos++]; }
void btn_flush(void) {}
void ms(int t) { tick_now+=t; }
void box(int x,int y,int w,int h,bool ink) { assert(y>=TOP); }
void outline(int x,int y,int w,int h,int t) { assert(y>=TOP); }
void text(int x,int y,int scale,const char *s,bool ink) { assert(y>=TOP); }
int cfg_get(const char *k,char *v,int n) {
    if(!strcmp(k,"badge_text")) { legacy_reads++; if(legacy_text) { snprintf(v,n,"%s",legacy_text); return strlen(v); } }
    if(n) *v=0; return -1;
}
bool cfg_get_image(uint8_t *out) { image_reads++; if(saved_picture) memset(out,0x5A,IMG_BYTES); return saved_picture; }
void badge_id(char id[5]) { strcpy(id,"1234"); }
void ranging_me(uint8_t mac[6]) { memset(mac,1,6); }
int ranging_get(range_t *out,int max) { return 0; }
bool ranging_name(const uint8_t mac[6],char name[13]) { return false; }
unsigned esp_random(void) { return random_state=random_state*1664525U+1013904223U; }
static int xSemaphoreCreateMutex(void) { return app_panel=next_mutex++; }
static int xSemaphoreCreateBinary(void) { return next_mutex++; }
static void xSemaphoreTake(int m,int t) { if(m==app_panel) assert(lock_depth++==0); }
static void xSemaphoreGive(int m) { if(m==app_panel) assert(--lock_depth==0); }
static void xTaskCreate(void (*fn)(void*),const char *name,int stack,void *arg,int priority,int *handle) { if(handle) *handle=1; }
static int64_t xTaskGetTickCount(void) { return tick_now; }
static void vTaskDelayUntil(TickType_t *last,TickType_t step) {
    if(infos && !delay_checks++) assert(*last==tick_now); // Snake's first post-modal delay starts at now
    *last+=step; tick_now=*last;
}
static void xTaskNotifyGive(TaskHandle_t t) { notifications++; }
int knob_angle(void) { return -1; }
static void esp_restart(void) { longjmp(stop,2); }
static void sequence(int a,int b,int c) { keys[0]=a;keys[1]=b;keys[2]=c;key_count=3;key_pos=0; }
static void reset(void) { infos=frames=wipes=closes=draws=fetches=key_pos=notifications=lock_depth=delay_checks=0;tick_now=0; }
'''
def run(name,code,tests):
 p=outdir/(name+'.c');p.write_text(common+code+tests)
 subprocess.run(['cc','-std=gnu11','-fsanitize=address,undefined','-I',str(root/'components/os/include'),str(p),'-lm','-o',str(p.with_suffix(''))],check=True)
 subprocess.run([str(p.with_suffix(''))],check=True)
# Full control functions and original rendering math, with panel/radio calls mocked.
run('source-app',clean(src('source')),r'''
int main(void) { for(int home=BTN_MINUS;home<=BTN_B;home++) { reset(); sequence(BTN_PLUS,home,home); app_source.run();
 assert(infos==1 && frames==1 && wipes==0 && !strcmp(labels[0],"Info") && !strcmp(labels[2],"Home") && !strcmp(labels[3],"Home")); }
 puts("PASS: Source Info returns to QR; all other old Home presses retained"); }
''')
run('life-app',clean(src('life')),r'''
int main(void) { for(int home=BTN_MINUS;home<=BTN_B;home++) { reset(); sequence(BTN_PLUS,home,home); app_life.run();
 assert(infos==1 && frames>=2 && !strcmp(labels[0],"Info") && !strcmp(labels[2],"Home") && !strcmp(labels[3],"Home")); }
 puts("PASS: Life resumes after Info; all other old Home presses retained"); }
''')
run('radar-app',clean(src('radar')),r'''
int main(void) { sequence(BTN_PLUS,BTN_A,BTN_B); app_radar.run();
 assert(infos==1 && wipes==1 && frames==3 && !strcmp(labels[0],"Info") && !strcmp(labels[2],"clear ghosts") && !strcmp(labels[3],"home"));
 puts("PASS: Radar Info, clear ghosts and Home; no radio takeover"); }
''')
s=src('snake'); globals=s[s.index('#define CELL'):s.index('static void fill_cell')]
run('snake-app',globals+'static int draw(void) { draws++; return 0; }\nstatic void draw_task(void *arg) {}\nstatic void run(void)'+s.split('static void run(void)',1)[1],r'''
int main(void) { info_requires_panel=true; keys[0]=BTN_A; keys[1]=BTN_PLUS;keys[2]=BTN_MINUS;keys[3]=BTN_B;key_count=4;
 if(!setjmp(stop)) run();
 assert(infos==1 && notifications==3 && delay_checks>0 && !lock_depth && !strcmp(labels[0],"Turn left") && !strcmp(labels[1],"Turn right") && !strcmp(labels[2],"Info"));
 puts("PASS: Snake Info pauses movement, restarts the clock and retains turns; unused B stays inert"); }
''')
s=src('weather')
code='''#define CAP 8192
#define EVERY_US (30 * 60 * 1000000LL)
static char lat[16],lon[16],place[64],city[48];
static bool fahr;
static TaskHandle_t drawer;
static SemaphoreHandle_t panel;
static struct { int gen; } wx;
typedef __typeof__(wx) wx_t;
static int notify_calls;
static void draw(const wx_t *w) { assert(lock_depth==1); draws++; }
static int ulTaskNotifyTake(int clear,int t) { assert(!lock_depth); if(++notify_calls==2) longjmp(stop,1); wx.gen++; return 1; }
static void *heap_caps_malloc(int n,int caps) { static char b[CAP]; return b; }
static int64_t esp_timer_get_time(void) { return tick_now*1000; }
static void fetch(char *buf) { assert(!lock_depth); fetches++; }
'''
code+=s[s.index('static void draw_task(void *arg)'):s.index('static void icon(int x, int y)')]
run('weather-app',code,r'''
int main(void) { info_requires_panel=true; sequence(BTN_PLUS,BTN_B,BTN_B); if(!setjmp(stop)) run();
 assert(infos==1 && fetches==3 && !lock_depth && !strcmp(labels[0],"Info") && !strcmp(labels[3],"Refresh"));
 // Actual drawer releases its mutex before waiting, and keeps its original one-wipe-on-new-data behavior.
 reset(); notify_calls=0; wx.gen=1; if(!setjmp(stop)) draw_task(NULL);
 assert(draws==2 && frames==2 && wipes==1 && !lock_depth);
 puts("PASS: Weather Info/Refresh plus drawer/wipe mutex; original fetch cadence and notification waits retained"); }
''')
s=src('settings');tail=s[s.index('    // The web server runs on its own task:'):s.index('static void icon(int x, int y)')]
run('settings-app','static volatile bool saved;\nstatic void settings_wait(void) {\n'+tail,r'''
int main(void) { sequence(BTN_PLUS,BTN_B,BTN_B); if(!setjmp(stop)) settings_wait();
 assert(infos==1 && saved && frames==1); saved=false; reset(); sequence(BTN_B,BTN_PLUS,BTN_PLUS);
 if(!setjmp(stop)) settings_wait(); assert(infos==0 && !saved && frames==0);
 puts("PASS: Settings phone save dismisses Info and restarts; existing B Home still restarts"); }
''')

print('App host harnesses:',outdir)
