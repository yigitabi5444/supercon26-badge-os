#pragma once
#include "../package_http.h"
typedef struct {
    const am_source_config_t *config;const char *code,*assets,*mode;
    unsigned clients,opens,cleans,clip_opens,clip_reads,max_read,joins,offs,syncs;
    bool cancelled,radio,locked,deferred;
} video_https_fixture_t;
extern video_https_fixture_t vh;
