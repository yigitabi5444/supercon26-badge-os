#pragma once
#include <stddef.h>
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef int httpd_err_code_t;
enum { HTTP_GET=1,HTTP_POST=2,HTTPD_403_FORBIDDEN=403,HTTPD_400_BAD_REQUEST=400,HTTPD_404_NOT_FOUND=404 };
typedef void *httpd_handle_t;
typedef struct {
    size_t content_len;const char *token,*host,*body;size_t position,length;
    char response[32768];int status;
} httpd_req_t;
typedef struct {const char *uri;int method;esp_err_t (*handler)(httpd_req_t *);void *user_ctx;} httpd_uri_t;
typedef struct {int stack_size,recv_wait_timeout;bool lru_purge_enable;} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
esp_err_t httpd_start(httpd_handle_t *,const httpd_config_t *);
esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t *);
esp_err_t httpd_register_err_handler(httpd_handle_t,int,esp_err_t (*)(httpd_req_t *,httpd_err_code_t));
size_t httpd_req_get_hdr_value_len(httpd_req_t *,const char *);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *,const char *,char *,size_t);
int httpd_req_recv(httpd_req_t *,char *,size_t);
esp_err_t httpd_resp_set_status(httpd_req_t *,const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *,const char *);
esp_err_t httpd_resp_set_hdr(httpd_req_t *,const char *,const char *);
esp_err_t httpd_resp_send(httpd_req_t *,const char *,int);
esp_err_t httpd_resp_sendstr(httpd_req_t *,const char *);
esp_err_t httpd_resp_send_chunk(httpd_req_t *,const char *,int);
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *,const char *);
esp_err_t httpd_resp_send_err(httpd_req_t *,int,const char *);
