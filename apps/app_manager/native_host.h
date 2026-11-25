#pragma once
#include <stdbool.h>
#include "txn.h"

// Home-task lifecycle. Start after relocation; retain ELF/copy/lease until stop succeeds.
am_result_t am_native_host_start(void);
am_result_t am_native_host_start_for(const char *id);
am_result_t am_native_host_stop(void); // AM_BUSY retains a live, stopping host; retry cleanup.
bool am_native_host_live(void);
bool am_native_host_good_return(void); // Evidence survives stop, resets at the next start.
