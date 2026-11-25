#pragma once
#include "native_messages.h"
am_result_t am_messages_bind(const am_native_messages_t *,const badge_native_api_t *);
const badge_native_api_t *am_messages_get(uint32_t);
am_result_t am_messages_end(void);
