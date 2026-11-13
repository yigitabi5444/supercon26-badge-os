#!/usr/bin/env python3
"""Actual Messaging service with host task/storage mocks; no device or radio access."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[3]
out=Path(tempfile.mkdtemp(prefix='badge-msg-unread-'))
(out/'freertos').mkdir()
(out/'freertos/FreeRTOS.h').write_text('''#pragma once
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY -1
#define pdMS_TO_TICKS(x) (x)
''')
(out/'freertos/queue.h').write_text('''#pragma once
#include <stddef.h>
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(int n, size_t size);
int xQueueReceive(QueueHandle_t q, void *p, int timeout);
int xQueueSend(QueueHandle_t q, const void *p, int timeout);
void xQueueReset(QueueHandle_t q);
''')
(out/'freertos/semphr.h').write_text('''#pragma once
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
void xSemaphoreTake(SemaphoreHandle_t lock, int timeout);
void xSemaphoreGive(SemaphoreHandle_t lock);
''')
(out/'freertos/task.h').write_text('''#pragma once
int xTaskCreate(void (*fn)(void *), const char *name, int stack, void *ctx, int priority, void *task);
void vTaskDelay(int ticks);
''')
(out/'esp_attr.h').write_text('#define DRAM_ATTR\n')
(out/'esp_timer.h').write_text('#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
code=r'''
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "messaging.c"
static message_state_t durable;
static bool fail_open, fail_write;
static int depth, takes, writes, receives;
static message_t incoming;
static jmp_buf worker_done;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void*)1; }
void xSemaphoreTake(SemaphoreHandle_t l,int t) { assert(depth++==0); takes++; }
void xSemaphoreGive(SemaphoreHandle_t l) { assert(--depth==0); }
QueueHandle_t xQueueCreate(int n,size_t size) { assert(n==6); return (void*)2; }
int xQueueReceive(QueueHandle_t q,void *p,int t) {
    assert(depth==0);
    if(receives++) longjmp(worker_done,1);
    memset(p,0,sizeof(packet_t)); return pdTRUE;
}
int xQueueSend(QueueHandle_t q,const void *p,int t) { return pdTRUE; }
void xQueueReset(QueueHandle_t q) {}
int xTaskCreate(void (*fn)(void*),const char *n,int stack,void *ctx,int pri,void *task) { return pdPASS; }
void vTaskDelay(int ticks) { assert(depth==0); }
int64_t esp_timer_get_time(void) { return 1000000; }
void ranging_me(uint8_t m[6]) { const uint8_t local[6]={2,0,0,0,0,1}; memcpy(m,local,6); }
int cfg_get(const char *key,char *value,int n) { snprintf(value,n,"Guest"); return 5; }
static int save(void *ctx,const message_state_t *s,uint64_t generation) {
    assert(depth==1); writes++; if(fail_write) return MSG_IO; durable=*s; return MSG_OK;
}
bool message_nvs_open(message_db_t *d,const char **why) {
    memset(d,0,sizeof *d); if(fail_open) return false;
    d->ready=true; d->state=durable; d->persist=save; return true;
}
bool message_frame(const uint8_t *p,size_t n) { return n>=4 && !memcmp(p,"BMSG",4); }
bool message_sync_receive(message_sync_t *s,const message_state_t *state,const uint8_t peer[6],const uint8_t *p,size_t n,uint64_t now,message_t *m) { *m=incoming; return true; }
void message_sync_tick(message_sync_t *s,const message_state_t *state,uint64_t now,message_send_fn fn) {}
static bool send_frame(const uint8_t peer[6],const uint8_t *p,size_t n) { return true; }
static message_t sample(int id,int thread) {
    message_t m={.logical=id}; m.id[0]=id; m.thread[0]=thread; m.sender[0]=2; m.sender[5]=2;
    strcpy(m.title,"Message"); strcpy(m.body,"Hello"); return m;
}
static void arrive(message_t m) {
    incoming=m; receives=0;
    if(!setjmp(worker_done)) worker(NULL);
    assert(depth==0);
}
static void cached(int expected) {
    int before=takes, before_writes=writes;
    for(int i=0;i<1000;i++) assert(messaging_unread()==expected);
    assert(takes==before && writes==before_writes); // top-bar reads never lock or persist
}
int main(void) {
    cached(-1); fail_open=true; assert(!messaging_start()); cached(-1);
    lock=NULL; fail_open=false; assert(messaging_start()); cached(0);
    messaging_transport(send_frame);
    arrive(sample(1,1)); cached(1); arrive(sample(2,1)); cached(2);
    int before=writes; arrive(sample(2,1)); cached(2); assert(writes==before);
    uint8_t local[16]={3}, thread[16]={1};
    assert(messaging_submit(local,thread,"Message","Local reply",0)==MSG_OK); cached(2);
    assert(messaging_read(thread)==MSG_OK); cached(0);
    before=writes; assert(messaging_read(thread)==MSG_DUP); cached(0); assert(writes==before);
    arrive(sample(4,4)); cached(1);
    // Simulated reboot reloads the durable unread flags through the actual startup path.
    lock=NULL; assert(messaging_start()); cached(1); messaging_transport(send_frame);
    fail_write=true; uint8_t other[16]={4}; assert(messaging_read(other)==MSG_IO); cached(-1);
    fail_write=false; lock=NULL; assert(messaging_start()); cached(1);
    assert(messaging_forget(other)==MSG_OK); cached(0);
    memset(&durable,0,sizeof durable); durable.count=MSG_CAP;
    for(int i=0;i<MSG_CAP;i++) durable.messages[i]=sample(i+10,i+10);
    lock=NULL; assert(messaging_start()); cached(MSG_CAP);
    puts("PASS actual service: unread startup/arrival/duplicate/local send/read/delete/reboot/write failure; 0..8/unknown cache reads without locks or writes");
}
'''
p=out/'service.c';p.write_text(code)
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-g','-I',str(out),'-I',str(root/'components/os/messages'),'-I',str(root/'components/os/include'),'-I',str(root/'apps/messaging'),str(p),str(root/'components/os/messages/message_store.c'),'-o',str(out/'service')],check=True)
subprocess.run([str(out/'service')],check=True)
print('Service harness:',out)
