#include "messaging.h"
#include "badge/badge.h"
#include <string.h>
#include <stdatomic.h>
#include "esp_attr.h"
#include "message_nvs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static message_db_t db;
// IDF's PSRAM workaround makes ATOMIC_INT_LOCK_FREE conservative. Internal DRAM
// uses its hardware load/store path; keep this four-byte cache out of PSRAM.
_Static_assert(sizeof(atomic_int) == 4, "Unread cache must use 32-bit atomics");
static DRAM_ATTR atomic_int unread_count = -1;
// Called with the store lock held, or during startup before transport attaches.
static void update_unread(void)
{
    int count = -1;
    if (db.ready) {
        count = 0;
        for (int i = 0; i < db.state.count; i++) if (!db.state.read[i]) count++;
    }
    atomic_store_explicit(&unread_count, count, memory_order_relaxed);
}
int messaging_unread(void) { return atomic_load_explicit(&unread_count, memory_order_relaxed); }
static message_sync_t sync_state;
static SemaphoreHandle_t lock;
static QueueHandle_t packets;
static message_send_fn transport;
static const char *status = "Message storage has not been initialized.";
typedef struct { uint8_t peer[6], data[MSG_PACKET_MAX]; uint16_t size; } packet_t;
static void result_status(int result)
{ if (result == MSG_IO) status = "Storage write failed. Reopen Messages after reboot."; update_unread(); }
static void worker(void *unused)
{
    packet_t p; message_t complete;
    for (;;) {
        bool got = xQueueReceive(packets, &p, pdMS_TO_TICKS(250)) == pdTRUE;
        xSemaphoreTake(lock, portMAX_DELAY);
        uint64_t now = esp_timer_get_time() / 1000;
        if (db.ready && transport) {
            if (got && message_sync_receive(&sync_state, &db.state, p.peer, p.data, p.size, now, &complete))
                result_status(message_put(&db, &complete, false));
            message_sync_tick(&sync_state, &db.state, now, transport);
        }
        xSemaphoreGive(lock);
        if (got) vTaskDelay(pdMS_TO_TICKS(20)); // cap processing under radio floods; queue drops excess
    }
}
bool messaging_start(void)
{
    if (lock) return db.ready;
    lock = xSemaphoreCreateMutex();
    if (!lock) return false;
    if (!message_nvs_open(&db, &status)) return false;
    packets = xQueueCreate(6, sizeof(packet_t));
    if (!packets || xTaskCreate(worker, "messages", 4096, NULL, 2, NULL) != pdPASS) {
        db.ready = false; status = "Messages unavailable: not enough memory."; return false;
    }
    update_unread(); status = "Ready"; return true;
}
void messaging_transport(message_send_fn send)
{
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY); transport = send; memset(&sync_state, 0, sizeof sync_state);
    if (packets) xQueueReset(packets);
    xSemaphoreGive(lock);
}
bool messaging_receive(const uint8_t peer[6], const uint8_t *data, size_t size)
{
    if (!message_frame(data, size)) return false;
    if (!packets || size < 22 || size > MSG_PACKET_MAX) return true;
    packet_t p = {.size = size}; memcpy(p.peer, peer, 6); memcpy(p.data, data, size);
    xQueueSend(packets, &p, 0); return true;
}
bool messaging_snapshot(message_state_t *out, uint64_t *revision)
{
    if (!lock) return false;
    xSemaphoreTake(lock, portMAX_DELAY); bool ok = db.ready; if (ok) { *out = db.state; if (revision) *revision = db.generation; }
    xSemaphoreGive(lock); return ok;
}
int messaging_submit(const uint8_t id[16], const uint8_t thread[16], const char *title, const char *body, uint64_t seconds)
{
    if (!lock || strlen(title) > MSG_TITLE || strlen(body) > MSG_TEXT) return MSG_INVALID;
    message_t m = {0}; memcpy(m.id, id, 16); memcpy(m.thread, thread, 16);
    strncpy(m.title, title, MSG_TITLE); strncpy(m.body, body, MSG_TEXT);
    ranging_me(m.sender); cfg_get("name", m.name, sizeof m.name);
    // Only preserve a nonempty printable Settings name, never credentials.
    for (char *p = m.name; *p; p++) if (*p < 32 || *p > 126) *p = '?';
    m.seconds = seconds; m.clock = seconds ? 1 : 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    int at = message_find(&db.state, id);
    if (at >= 0) {
        // Idempotent browser retry preserves the sender snapshot and logical order assigned on first acceptance.
        message_t *old = &db.state.messages[at];
        if (memcmp(old->sender, m.sender, 6)) { xSemaphoreGive(lock); return MSG_CONFLICT; }
        memcpy(m.name, old->name, sizeof m.name); m.logical = old->logical;
    } else m.logical = message_next_logical(&db.state);
    int result = message_put(&db, &m, true); result_status(result);
    xSemaphoreGive(lock); return result;
}
int messaging_read(const uint8_t thread[16])
{
    if (!lock) return MSG_IO;
    xSemaphoreTake(lock, portMAX_DELAY); int result = message_mark_read(&db, thread); result_status(result);
    xSemaphoreGive(lock); return result;
}
int messaging_forget(const uint8_t thread[16])
{
    if (!lock) return MSG_IO;
    xSemaphoreTake(lock, portMAX_DELAY); int result = message_forget(&db, thread); result_status(result);
    xSemaphoreGive(lock); return result;
}
int messaging_relay(bool enabled)
{
    if (!lock) return MSG_IO;
    xSemaphoreTake(lock, portMAX_DELAY); int result = message_relay(&db, enabled); result_status(result);
    if (!enabled) memset(&sync_state, 0, sizeof sync_state);
    xSemaphoreGive(lock); return result;
}
const char *messaging_status(void) { return status; }
