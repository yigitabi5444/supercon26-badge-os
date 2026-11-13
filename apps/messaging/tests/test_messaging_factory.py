#!/usr/bin/env python3
"""Actual adapter/recovery guard and protected-source checks; optional read-only stock factory evidence. No flash."""
from pathlib import Path
import hashlib,sys,subprocess,tempfile,os
root=Path(__file__).resolve().parents[3]
base='b43e45087abb0604263d3edc84d0c0fcdd47f6dc'
for name in ['partitions.csv','sdkconfig.defaults','apps/update/ota.c','apps/pong/link.h']:
 assert subprocess.check_output(['git','show',base+':'+name],cwd=root)==(root/name).read_bytes(),name
for name in ['apps/update/update.c','apps/pong/pong.c']:
 original=subprocess.check_output(['git','show',base+':'+name],cwd=root,text=True)
 assert original.split('const app_t ')[0]==(root/name).read_text().split('const app_t ')[0],name
adapter=(root/'components/os/messages/message_nvs.c').read_text()
assert 'nvs_open("badge_msg", NVS_READWRITE' in adapter and 'nvs_set_blob(handle, "state"' in adapter
for forbidden in ['esp_flash_','esp_partition_','nvs_flash_erase','nvs_erase_all','nvs_erase_key']:assert forbidden not in adapter
out=Path(tempfile.mkdtemp(prefix='badge-msg-adapter-'))
(out/'nvs.h').write_text('''#pragma once
#include <stddef.h>
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef struct {size_t available_entries;} nvs_stats_t;
enum {ESP_OK,ESP_ERR_NVS_NOT_FOUND,ESP_ERR_NVS_INVALID_LENGTH,ESP_ERR_NVS_TYPE_MISMATCH,ESP_ERR_NVS_NOT_ENOUGH_SPACE};
#define NVS_READWRITE 1
int nvs_open(const char*,int,nvs_handle_t*);void nvs_close(nvs_handle_t);
int nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);
int nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);int nvs_commit(nvs_handle_t);
int nvs_get_stats(const char*,nvs_stats_t*);int nvs_get_used_entry_count(nvs_handle_t,size_t*);
''')
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "message_nvs.h"
#include "nvs.h"
static bool ready;static int opens,closes,open_error,blob_result;
static message_blob_io_t captured;
bool cfg_storage_ready(void){return ready;}
int nvs_open(const char *name,int mode,nvs_handle_t *h){assert(!strcmp(name,"badge_msg")&&mode==NVS_READWRITE);opens++;*h=42;return open_error;}
void nvs_close(nvs_handle_t h){assert(h==42);closes++;}
int nvs_get_blob(nvs_handle_t h,const char *name,void *data,size_t *n){assert(h==42&&!strcmp(name,"state"));return ESP_ERR_NVS_NOT_FOUND;}
int nvs_set_blob(nvs_handle_t h,const char *name,const void *data,size_t n){assert(h==42&&!strcmp(name,"state"));return ESP_OK;}
int nvs_commit(nvs_handle_t h){assert(h==42);return ESP_OK;}
int nvs_get_stats(const char *name,nvs_stats_t *s){assert(!name);s->available_entries=300;return ESP_OK;}
int nvs_get_used_entry_count(nvs_handle_t h,size_t *n){assert(h==42);*n=0;return ESP_OK;}
int message_blob_open(message_db_t *db,message_blob_backend_t *backend,message_blob_io_t io){captured=io;return blob_result;}
#include "message_nvs.c"
int main(void){
 message_db_t db={0};const char *why=NULL;
 assert(!message_nvs_open(&db,&why)&&opens==0&&strstr(why,"preserved"));
 ready=true;open_error=7;assert(!message_nvs_open(&db,&why)&&opens==1&&closes==0);
 open_error=ESP_OK;blob_result=MSG_OK;assert(message_nvs_open(&db,&why)&&opens==2);
 uint8_t data[16]={0};size_t n=sizeof data;
 assert(captured.load(NULL,data,&n)==0&&captured.save(NULL,data,n)==MSG_OK);
 assert(captured.available(NULL)==300&&captured.empty(NULL));
 blob_result=MSG_FORMAT;assert(!message_nvs_open(&db,&why)&&closes==1&&strstr(why,"preserved"));
 blob_result=MSG_FULL;assert(!message_nvs_open(&db,&why)&&closes==2&&strstr(why,"Settings"));
 puts("PASS actual NVS adapter: recovery readiness blocks open, only badge_msg/state, committed save, format/full retained with explicit status");
}
'''
p=out/'adapter.c';p.write_text(code)
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-g','-I',str(out),'-I',str(root/'components/os/messages'),'-I',str(root/'components/os/include'),str(p),'-o',str(out/'adapter')],check=True)
subprocess.run([str(out/'adapter')],check=True)
print('PASS pinned v1.2 partitions/config, update write logic and Pong protocol unchanged (app Info metadata excluded)')
ref=sys.argv[1] if len(sys.argv)>1 else os.environ.get('BADGE_FACTORY_IMAGE')
if not ref:
 print('SKIP stock factory binary evidence: supply a public reference path as argv[1] or BADGE_FACTORY_IMAGE; never copy it into the repo.')
else:
 b=Path(ref).read_bytes();assert len(b)==0x800000
 assert hashlib.sha256(b).hexdigest()=='4065f070deebc56e33f3bbdbfbc553af043a2cdd2d270c37fb062360712bd083'
 assert any(x!=255 for x in b[0x650000:0x659000])
 assert not any(b[a:a+4096]==b'\xff'*4096 for a in range(0x650000,0x800000,4096))
 for ranges in [[(0,0x200000)],[(0,0x8000),(0x8000,0x9000),(0xf000,0x11000),(0x20000,0x320000)],[(0x20000,0x320000)],[(0x320000,0x620000)]]:
  image=bytearray(b)
  for a,z in ranges:image[a:z]=bytes(z-a)
  assert image[0x650000:]==b[0x650000:]
 print('PASS public stock factory reference: occupied tail preserved by bounded full-image/wired/both-slot write simulations')
print('LIMIT: merged factory images span NVS; firmware-only update is required for settings/messages preservation. No physical OTA/rollback result claimed.')
print('NVS adapter harness:',out)
