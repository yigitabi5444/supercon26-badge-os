#pragma once
#include "native_hackaday.h"

bool am_had_access(void *,bool frame);
void am_had_fault(void);
void am_had_note_release(void);
int32_t am_had_key(void *,uint32_t);
bool am_had_closing(void);
void am_had_apply_keys(void);
am_result_t am_had_bind(const am_native_hackaday_t *,const badge_native_api_t *);
const badge_native_api_t *am_had_get(uint32_t);
am_result_t am_had_end(void);
