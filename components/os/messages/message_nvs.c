#include "message_nvs.h"
#include "nvs.h"
#include "badge/settings.h"
static nvs_handle_t handle;
static message_blob_backend_t backend;
static int load(void *unused, uint8_t *out, size_t *size)
{
    esp_err_t e = nvs_get_blob(handle, "state", out, size);
    if (e == ESP_ERR_NVS_NOT_FOUND) return 0;
    if (e == ESP_ERR_NVS_INVALID_LENGTH || e == ESP_ERR_NVS_TYPE_MISMATCH) return -2;
    return e == ESP_OK ? 1 : -1;
}
static int save(void *unused, const uint8_t *data, size_t size)
{
    esp_err_t e = nvs_set_blob(handle, "state", data, size);
    if (e == ESP_ERR_NVS_NOT_ENOUGH_SPACE) return MSG_FULL;
    if (e == ESP_OK) e = nvs_commit(handle);
    return e == ESP_OK ? MSG_OK : MSG_IO;
}
static size_t available(void *unused)
{ nvs_stats_t s; return nvs_get_stats(NULL, &s) == ESP_OK ? s.available_entries : 0; }
static bool empty(void *unused)
{ size_t used; return nvs_get_used_entry_count(handle, &used) == ESP_OK && used == 0; }
bool message_nvs_open(message_db_t *db, const char **status)
{
    *status = "Messages unavailable: NVS needs non-destructive recovery; data preserved.";
    if (!cfg_storage_ready()) return false;
    if (nvs_open("badge_msg", NVS_READWRITE, &handle) != ESP_OK) return false;
    int result = message_blob_open(db, &backend, (message_blob_io_t){NULL,load,save,available,empty});
    if (result == MSG_OK) return true;
    nvs_close(handle);
    if (result == MSG_FORMAT) *status = "Message data has a newer or damaged format; preserved, not reset.";
    if (result == MSG_FULL) *status = "Not enough message space. Settings space is reserved; existing data preserved.";
    return false;
}
