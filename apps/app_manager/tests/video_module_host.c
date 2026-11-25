// Host-only visibility for registering the original module's RAM buffers with SDK pointer doubles.
// Preserve the module source bytes; this is never used by the standalone Xtensa build.
#include "badge_video_api.h"
#include "video_playback.h"
#define app_main video_test_main
#include "../modules/video/video_module.c"
#undef app_main
uint8_t *video_test_compressed(void){return compressed;}
uint8_t *video_test_pixels(void){return pixels;}
