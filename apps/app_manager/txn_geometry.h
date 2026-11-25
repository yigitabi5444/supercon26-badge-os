#pragma once
#include "txn.h"
enum { AM_PARTITIONS_MAX = 32 };
typedef struct {
    uint64_t address,size;uint8_t type,subtype;bool encrypted,readonly;char label[17];
} am_partition_view_t;
typedef struct {
    bool qualified;uint64_t address,size,flash_size;uint8_t table_sector_sha256[32];
    uint32_t reserve_bytes;
    uint8_t legacy_table_sector_sha256[32];
} am_layout_policy_t;
am_result_t am_geometry_check(const am_layout_policy_t *,const am_partition_view_t *,size_t,
                              uint64_t,const uint8_t[32]);
