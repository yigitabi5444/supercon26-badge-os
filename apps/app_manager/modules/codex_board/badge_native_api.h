#pragma once
#include <stdint.h>

#define BADGE_NATIVE_ABI 2u
#define BADGE_NATIVE_INFO_MAGIC 0x32504142u /* BAP2 */
enum { NATIVE_PLUS=1u, NATIVE_MINUS=2u, NATIVE_A=4u, NATIVE_B=8u };
typedef struct {
    uint32_t held,pressed;
    int32_t knob; /* 0..4095, or -1 when absent */
    uint32_t exit_requested; /* Both lower buttons held600ms; module returns normally. */
} badge_native_input_t;
typedef struct {
    uint32_t magic,abi;
    char id[16],version[24]; /* NUL terminated and zero padded; signed-manifest identity. */
} badge_native_info_t;
#define BADGE_NATIVE_INFO(id_,version_) \
    static const badge_native_info_t badge_native_info \
    __attribute__((used,section(".badge_app"),aligned(4))) = \
    { BADGE_NATIVE_INFO_MAGIC,BADGE_NATIVE_ABI,id_,version_ }

typedef struct {
    uint32_t abi_version,struct_size;
    void *context;
    uint32_t width,height,top;
    uint32_t (*now_ms)(void *);
    void (*wait_ms)(void *,uint32_t); /* At most3000ms; Home stays immediate. */
    void (*input)(void *,badge_native_input_t *);
    uint32_t (*random)(void *);
    int32_t (*begin_frame)(void *); /* Nonblocking; false while host drawer owns fb. */
    void (*submit_frame)(void *); /* Paired with successful begin, same owning task. */
    void (*clear)(void *); /* White playfield below top; frame lease required. */
    void (*box)(void *,int32_t,int32_t,int32_t,int32_t,uint32_t);
    void (*outline)(void *,int32_t,int32_t,int32_t,int32_t,int32_t,uint32_t);
    void (*text)(void *,int32_t,int32_t,int32_t,const char *,uint32_t);
    void (*frame_idle)(void *); /* Without frame lease: drain submitted frame and ui_close. */
    void (*wipe)(void *); /* Without frame lease: drain then existing fast ui_wipe. */
} badge_native_api_t;
_Static_assert(sizeof(badge_native_input_t)==16,"Native input wire layout");
_Static_assert(sizeof(badge_native_info_t)==48,"Native metadata wire layout");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(badge_native_api_t)==72,"ESP32-S3 nativeABI2 layout");
#endif
/* The module's sole permitted host import. No SDK/libc/RTOS/task exports. */
__attribute__((visibility("default")))
const badge_native_api_t *badge_app_get_api(uint32_t abi,uint32_t minimum_size);
