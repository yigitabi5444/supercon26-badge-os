// ELF note is checked by native_elf.c against the real ELF; host entry is Mach-O.
#include "../modules/codex_board/badge_codex_api.h"
#undef BADGE_NATIVE_INFO
#define BADGE_NATIVE_INFO(id,version) _Static_assert(1,"ELF note checked separately")
#define app_main cb_test_main
#include "../modules/codex_board/codex_board_module.c"
