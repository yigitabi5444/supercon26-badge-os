// Run the exact native entry in a host executable; actual ELF metadata is checked separately.
#include "../modules/hackaday/badge_hackaday_api.h"
#undef BADGE_NATIVE_INFO
#define BADGE_NATIVE_INFO(id,version) _Static_assert(1,"Metadata checked on actual ELF")
#define app_main had_test_main
#include "../modules/hackaday/hackaday_module.c"
