// Host-owned Video helpers; no module callbacks survive a call or unload.
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "badge/badge.h"
#include "stb_image.h"
#include "badge_video_api.h"
#include "native_video.h"

int32_t am_native_video_inflate(uint8_t *out,uint32_t capacity,const uint8_t *in,uint32_t size)
{
    if(!out || !in || capacity!=BADGE_VIDEO_PIXELS || !size || size>BADGE_VIDEO_COMPRESSED_MAX)return -1;
    return stbi_zlib_decode_buffer((char *)out,(int)capacity,(const char *)in,(int)size);
}
bool am_native_video_info(void)
{
    enum { BODY=(H-TOP)*(W/8) };
    uint8_t *saved=heap_caps_malloc(BODY,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!saved)return false; // Preserve the actual video frame when Info cannot allocate its backup.
    memcpy(saved,fb+TOP*(W/8),BODY);
    ui_keys("Back","Back","Back","Back");
    box(0,TOP,W,H-TOP,false);
    text(32,TOP+32,3,"Video",true);
    text(32,TOP+112,2,"15-second monochrome clip at 4 fps.",true);
    text(32,TOP+160,2,"A restarts. B returns Home.",true);
    text(32,TOP+240,2,"Badge OS: apps/video/video.c",true);
    text(32,TOP+288,2,"Clip: apps/video/tools",true);
    text(32,H-64,2,"Any button: back",true);
    ui_frame(1);ui_close();
    btn_flush();btn_get(-1);
    vTaskDelay(pdMS_TO_TICKS(20)?pdMS_TO_TICKS(20):1);
    while(btn_held())vTaskDelay(1);
    btn_flush();
    memcpy(fb+TOP*(W/8),saved,BODY);
    heap_caps_free(saved);
    return true;
}
