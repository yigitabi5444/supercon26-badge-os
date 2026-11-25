#!/usr/bin/env python3
"""Run the actual stock v1.2 and candidate online-update flow with SDK doubles.
The SDK double models app-only writes; this is software call-flow evidence, not OTA hardware qualification.
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
stock=subprocess.check_output(['git','show','b43e45087abb0604263d3edc84d0c0fcdd47f6dc:apps/update/ota.c'],cwd=root,text=True)
candidate=(root/'apps/update/ota.c').read_text()
def function(source,name):
 start=source.index('static '+name);opening=source.index('{',start);depth=1;end=opening+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
preamble=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define URL "https://example.invalid/badge_os.bin"
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_HTTPS_OTA_IN_PROGRESS 1
#define ESP_LOGE(...) ((void)0)
typedef int esp_err_t;
typedef struct { const char *version; } esp_app_desc_t;
typedef struct { const char *url;int (*crt_bundle_attach)(void*);int (*event_handler)(void*);int buffer_size,buffer_size_tx,timeout_ms;bool keep_alive_enable; } esp_http_client_config_t;
typedef struct { const esp_http_client_config_t *http_config; } esp_https_ota_config_t;
typedef void *esp_https_ota_handle_t;
static int nlines,want,pct,status,begin_calls,perform_calls,aborts,finishes,resets;
static bool stop,legacy_allowed;static void *drawer;static int64_t t0;
static const char *offered;static uint8_t flash[0x800000],before[0x800000];
static const esp_app_desc_t *esp_app_get_description(void) { static esp_app_desc_t d={"1.2"};return &d; }
static int64_t esp_timer_get_time(void) { return 0; }
static void draw_task(void *p) { (void)p; }
static void *xTaskGetCurrentTaskHandle(void) { return NULL; }
static int xTaskCreate(void(*f)(void*),const char*n,int stack,void*arg,int priority,void**task)
{ (void)f;(void)n;(void)stack;(void)arg;(void)priority;*task=NULL;return 1; }
static void say(int scale,const char*s) { (void)scale;assert(s); }
static void lap(const char*s) { (void)s;(void)t0; }
static void done(const char*a,const char*b) { assert(a);(void)b; }
static bool wifi_connect(int ms) { assert(ms==20000);return true; }
static int esp_crt_bundle_attach(void*p) { (void)p;return 0; }
static int on_http(void*p) { (void)p;return 0; }
static const char *esp_err_to_name(int n) { (void)n;return "test"; }
static int esp_https_ota_begin(const esp_https_ota_config_t*c,void**h) { assert(c->http_config);*h=(void*)1;begin_calls++;return 0; }
static int esp_https_ota_get_img_desc(void*h,esp_app_desc_t*d) { assert(h);d->version=offered;return 0; }
static int esp_https_ota_abort(void*h) { assert(h);aborts++;return 0; }
static int esp_https_ota_get_image_size(void*h) { assert(h);return 1700000; }
static int esp_https_ota_perform(void*h) { assert(h);perform_calls++;memset(flash+0x320000,0xa5,1700000);return 0; }
static int esp_https_ota_get_image_len_read(void*h) { assert(h);return 1700000; }
static bool esp_https_ota_is_complete_data_received(void*h) { assert(h);return true; }
static int esp_https_ota_finish(void*h) { assert(h);finishes++;return 0; }
static void redraw(void) {}
static void finish_screen(void) {}
static void esp_restart(void) { resets++; }
"""
checks=r"""
static void check(const char*version,bool allowed,bool expect_install,bool expect_begin) {
 offered=version;legacy_allowed=allowed;begin_calls=perform_calls=aborts=finishes=resets=0;
 memset(flash,0x3c,sizeof flash);memcpy(before,flash,sizeof flash);run();
 assert(begin_calls==(int)expect_begin && perform_calls==(int)expect_install);
 assert(finishes==(int)expect_install && resets==(int)expect_install);
 if(!expect_install)assert(!memcmp(before,flash,sizeof flash));
 else { assert(!memcmp(before,flash,0x320000));assert(!memcmp(before+0x4bf0a0,flash+0x4bf0a0,0x800000-0x4bf0a0)); }
}
"""
with tempfile.TemporaryDirectory(prefix='badge-ota-compat-') as d:
 tmp=Path(d)
 for name,source in [('stock',stock),('candidate',candidate)]:
  code=preamble
  if name=='candidate':code+='static bool update_layout_local_legacy(void) { return legacy_allowed; }\n'
  code+=function(source,'bool newer(')+'\n'+function(source,'void run(')+'\n'+checks
  code+='int main(void) { check("1.2",true,false,true);check("1.3",true,true,true);'
  if name=='candidate':code+='check("1.3",false,false,false);'
  code+='puts("PASS '+name+': same-version skip, newer app-only flow'+(', managed-layout refusal' if name=='candidate' else '')+'");}\n'
  c=tmp/(name+'.c');exe=tmp/name;c.write_text(code)
  subprocess.run(['cc','-std=c11','-fno-common','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
# Release uses the legacy profile, while the fresh profile is separately selected.
assert 'idf.py -DBADGE_APP_MANAGER=OFF build' in (root/'.github/workflows/release.yml').read_text()
assert 'appstore' not in (root/'partitions.csv').read_text()
assert 'FLASH_IN_PROJECT' not in (root/'CMakeLists.txt').read_text()
print('PASS release routing: legacy OTA selection; separate fresh table; empty store excluded from normal flash')
