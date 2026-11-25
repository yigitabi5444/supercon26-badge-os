#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef struct http_double *esp_http_client_handle_t;
enum { HTTP_EVENT_ON_HEADER=3, HTTP_TRANSPORT_OVER_SSL=2 };
typedef struct {
    int event_id;esp_http_client_handle_t client;void *user_data;char *header_key,*header_value;
} esp_http_client_event_t;
typedef struct {
    const char *url;esp_err_t (*crt_bundle_attach)(void *);int transport_type,timeout_ms;
    int buffer_size,buffer_size_tx;bool disable_auto_redirect,skip_cert_common_name_check;
    bool use_global_ca_store;const char *common_name;
    esp_err_t (*event_handler)(esp_http_client_event_t *);void *user_data;const char *user_agent;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t,int);
esp_err_t esp_http_client_open(esp_http_client_handle_t,int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
bool esp_http_client_is_chunked_response(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t,char *,int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t,const char *);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
