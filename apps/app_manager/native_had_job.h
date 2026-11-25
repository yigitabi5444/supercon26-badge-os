#pragma once
#include <stdatomic.h>
#include "native_hackaday.h"
// Resident job input/result only. No module buffer, allocation or callback here.
typedef struct {
    atomic_bool cancel;
    atomic_uint status,joined,join_ms,bytes;
    uint32_t kind,box_w,box_h,width,height,length;
    uint8_t *data;
    char url[400],error[64];
} am_had_job_t;
bool am_had_fetch_job(am_had_job_t *);
bool am_had_decode(am_had_job_t *,const uint8_t *,uint32_t);
bool am_had_inflate(am_had_job_t *,const uint8_t *,uint32_t);
