#pragma once
#include "txn.h"

enum { AM_NATIVE_PAYLOAD_MAX = 65536, AM_NATIVE_MEMORY_MAX = 131072 };
// Preflight for the pinned ESP32-S3 elf_loader2.1.0 section loader, not a sandbox.
// Small apps use a private launch copy. Doom uses a bounded temporary preflight
// buffer, freed before relocation from the same verified immutable store lease.
am_result_t am_native_elf_check(am_bytes_t, const char *app_id,
                                const char *version, uint32_t abi);
