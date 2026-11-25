#pragma once
#include "native_codex.h"

// App-internal bridge to the shared frame owner, never part of the module API/resolver.
bool am_codex_access(void *,bool frame);
void am_codex_fault(void);
int32_t am_codex_button(void *,uint32_t);
void am_codex_note_release(void);
am_result_t am_codex_bind(const am_native_codex_t *,const badge_native_api_t *);
const badge_native_api_t *am_codex_get(uint32_t);
void am_codex_end(void);
