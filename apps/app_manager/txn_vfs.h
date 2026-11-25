#pragma once
#include "txn.h"
enum { AM_VFS_BASE_MAX = 256, AM_VFS_PATH_MAX = AM_VFS_BASE_MAX + AM_PATH_MAX };
typedef struct { char root[AM_VFS_BASE_MAX];uint64_t quota; } am_vfs_t;
// Ordinary POSIX/VFS primitives only. Platform supplies lock and durability overrides.
am_result_t am_vfs_io(am_vfs_t *,const char *,uint64_t,am_io_t *);
am_result_t am_vfs_readonly_io(am_vfs_t *,const char *,uint64_t,am_io_t *); // Existing directory only; mutators disabled.
