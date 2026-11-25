#!/usr/bin/env python3
"""Actual cfg_get/wifi_connect SSID boundary regression, mocked NVS and radio only."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'components/os/wifi.c').read_text()
settings=(root/'components/os/settings.c').read_text()
pre='\n#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n#include <stdlib.h>\n#define ESP_OK 0\n#define NVS_READONLY 0\n#define WIFI_SSID ""\n#define WIFI_PASS ""\n#define WIFI_IF_STA 0\n#define WIFI_PS_NONE 0\n#define pdMS_TO_TICKS(x) (x)\ntypedef int nvs_handle_t;\ntypedef int esp_err_t;\ntypedef struct { struct { uint8_t ssid[32],password[64]; } sta; } wifi_config_t;\nstatic bool storage_ready=true,wifi_join;\nstatic int wifi_ev,starts;\nstatic char saved_ssid[34];\nstatic uint8_t configured[32];\nstatic int nvs_open(const char *ns,int mode,nvs_handle_t *h) { *h=1; return ESP_OK; }\nstatic void nvs_close(nvs_handle_t h) {}\nstatic int nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *len) {\n const char *value=!strcmp(key,"ssid")?saved_ssid:"password";size_t need=strlen(value)+1;\n if(*len<need) { *len=need;return 0x110c; }\n memcpy(out,value,need);*len=need;return ESP_OK;\n}\nstatic void wifi_on(void) { starts++; }\nstatic int esp_wifi_set_config(int iface,const wifi_config_t *cfg) { memcpy(configured,cfg->sta.ssid,32); assert(!strcmp((const char*)cfg->sta.password,"password")); return 0; }\nstatic void esp_wifi_set_ps(int mode) {}\nstatic void esp_wifi_connect(void) {}\nstatic unsigned xEventGroupWaitBits(int ev,unsigned bits,bool clear,bool all,int timeout) { return bits; }\n'
cfg=settings[settings.index('int cfg_get('):settings.index('bool cfg_set(')]
join=s[s.index('bool wifi_connect('):s.index('bool wifi_connected(')]
tests=r'''
int main(void) {
 memset(saved_ssid,'x',31);saved_ssid[31]=0;assert(wifi_connect(1000)&&starts==1&&configured[31]==0);
 saved_ssid[31]='y';saved_ssid[32]=0;assert(wifi_connect(1000)&&starts==2&&!memcmp(configured,saved_ssid,32));
 for(int i=0;i<16;i++)memcpy(saved_ssid+2*i,"\xc4\xb0",2);saved_ssid[32]=0;
 assert(wifi_connect(1000)&&starts==3&&!memcmp(configured,saved_ssid,32));
 saved_ssid[0]=0;assert(!wifi_connect(1000)&&starts==3);
 memset(saved_ssid,'x',33);saved_ssid[33]=0;assert(!wifi_connect(1000)&&starts==3);
 puts("PASS: actual cfg_get/wifi_connect 31/32-byte and UTF-8 names, exact driver bytes/terminator, empty/overlong refusal; no storage writes");
}
'''
out=Path(tempfile.mkdtemp(prefix='wifi-ssid-'));p=out/'test.c';p.write_text(pre+cfg+join+tests)
subprocess.run(['cc','-std=gnu11','-fsanitize=address,undefined',str(p),'-o',str(out/'test')],check=True)
subprocess.run([str(out/'test')],check=True)
