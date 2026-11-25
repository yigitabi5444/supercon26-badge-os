#pragma once
#include "package_source.h"
enum { AM_FETCH_MS = 20000, AM_HTTP_IO_MS = 1000, AM_WIFI_MS = 5000, AM_REDIRECT_MAX = 2 };
typedef void (*am_progress_t)(const char *);
typedef struct {
    int64_t deadline;bool (*cancel)(void *);void *owner;const am_source_config_t *config;
    bool radio_owned,location_seen,header_bad;char location[AM_URL_MAX+1];
} am_https_t;
bool am_https_enabled(void); // Refuse fetching if SDK certificate date checks were compiled out.
bool am_https_clock_ready(void);
// Trusted app jobs only; preserves date checks and the shared Clock freshness stamp.
am_result_t am_https_link_ready(bool (*)(void *),void *,am_progress_t,uint32_t wifi_timeout_ms);
am_result_t am_https_prepare(const am_source_config_t *,bool (*)(void *),void *,am_progress_t);
am_result_t am_https_begin(am_https_t *,const am_source_config_t *,bool (*)(void *),void *,am_progress_t);
am_result_t am_https_get(void *,const char *,uint8_t *,size_t,size_t *);
am_result_t am_https_doom_get(void *,const char *,uint8_t *,size_t,size_t *); // Internal measured Doom acquisition only.
void am_https_end(am_https_t *); // All HTTP clients already cleaned up, then Wi-Fi off/ranging restored.
typedef struct { am_https_t *session;void *client;uint32_t offset; } am_https_video_t;
// Exactly the enrolled Video clip length, sequential <=4096B reads; ELF fetch cap stays unchanged.
am_result_t am_https_video_open(am_https_t *,const char *,am_https_video_t *);
am_result_t am_https_video_read(void *,uint32_t,uint8_t *,size_t,size_t *);
am_result_t am_https_video_close(am_https_video_t *);
