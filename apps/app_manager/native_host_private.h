#pragma once
#include <stdbool.h>
#include <stdint.h>
// App-internal ownership hooks, never exported through the module resolver.
enum { AM_NATIVE_CODEX=1,AM_NATIVE_HACKADAY=2,AM_NATIVE_MESSAGES=3,AM_NATIVE_DOOM=4 };
bool am_native_access(void *,unsigned,bool frame);
void am_native_fault(void);
void am_native_note_release(unsigned);
int32_t am_native_key(void *,uint32_t,bool exit_on_b,bool force_exit);
int32_t am_native_info(void *,unsigned,int32_t (*)(void *),void *);
