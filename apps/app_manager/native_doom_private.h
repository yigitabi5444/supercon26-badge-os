#pragma once
#include "native_doom.h"
am_result_t am_doom_bind(const am_native_doom_t *,const badge_native_api_t *);
const badge_native_api_t *am_doom_get(uint32_t);
void am_doom_end(void);
