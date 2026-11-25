// Exact controller bytes, with test-only registration of module-owned RAM.
#include "../modules/codex_board/cb_native_controller.c"
#include "native_host_double.h"
void cb_test_memory(void)
{
    nh_memory(&transport,sizeof transport,NH_PSRAM,true);
    nh_memory(saved,sizeof saved,NH_PSRAM,true);
    nh_memory(&current,sizeof current,NH_PSRAM,true);
    nh_memory(&received,sizeof received,NH_PSRAM,true);
    nh_memory(input,sizeof input,NH_PSRAM,true);
}
