#pragma once
#include "txn.h"

enum { AM_VIDEO_CLIP_BYTES=819646, AM_VIDEO_RECEIPT_BYTES=56, AM_ASSET_CHUNK=4096 };
#define AM_VIDEO_CLIP_HASH "c804c2cbb5df2830f17a814d65be2fab229b0825d6e89d089a64551d5d6d6df7"
typedef struct { am_store_t *store;am_stream_hash_t hash; } am_video_store_t;
typedef struct { am_store_t *store;const am_lease_t *lease;uint32_t generation; } am_video_asset_t;

// Private BVA1 receipt: magic4, zero-padded owner16, LE length4, SHA25632.
// Exactly the measured original clip; separate from unchanged native ELF manifests/ABI2.
am_result_t am_video_asset_install(am_video_store_t *,am_bytes_t receipt,am_bytes_t signature,
                                   am_reader_t source,void *source_context);
// Borrow the existing execution/read lease; never reacquire its nonrecursive lock.
am_result_t am_video_asset_open(am_video_store_t *,const am_lease_t *,const char *hash,am_video_asset_t *);
am_result_t am_video_asset_read(const am_video_asset_t *,uint32_t,uint8_t *,size_t);
bool am_video_asset_live(const am_video_asset_t *); // No I/O; owner and lease generation still match.
am_result_t am_video_asset_check(am_video_store_t *); // Verify installed asset under the store lock.
