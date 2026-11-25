#pragma once
// Private bounded native-app transactions. No partition/mount/module ABI is exposed.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { AM_MANIFEST_MAX = 4096, AM_SIGNATURE_MAX = 128, AM_PAYLOAD_MAX = 65536, AM_PAYLOAD_SIZE = AM_PAYLOAD_MAX,
       AM_TOKEN_SIZE = 65, AM_PATH_MAX = 96, AM_VERSIONS_MAX = 8 };
enum { AM_DOOM_PAYLOAD_MAX=328676, AM_DOOM_MEMORY_MAX=529227, AM_DOOM_RELOCS_MAX=3589 };
typedef enum {
    AM_OK, AM_DISABLED, AM_NOT_FOUND, AM_BAD_MANIFEST, AM_BAD_AUTH, AM_BAD_HASH,
    AM_BAD_RECORD, AM_IO, AM_FULL, AM_BUSY, AM_CANCELLED, AM_OLD_VERSION, AM_LIMIT,
    AM_WIFI_FAILED, AM_TIME_FAILED, AM_DOWNLOAD_FAILED, AM_UP_TO_DATE
} am_result_t;
typedef struct {
    char id[16], version[21], target[24], sdk[24], loader[24], hash[65];
    uint32_t abi, size, parts[3];
} am_metadata_t;
typedef struct { const char *target, *sdk, *loader; uint32_t abi; } am_runtime_t;
typedef struct { const uint8_t *data; size_t size; } am_bytes_t;
typedef struct { am_bytes_t manifest, signature, payload; } am_package_t;
typedef am_result_t (*am_reader_t)(void *,uint32_t,uint8_t *,size_t,size_t *);
typedef am_result_t (*am_stream_hash_t)(am_reader_t,void *,uint32_t,uint8_t[32]);
typedef struct {
    void *context;
    am_result_t (*sha256)(void *, const uint8_t *, size_t, uint8_t[32]);
    am_result_t (*verify_p256)(void *, const uint8_t *, size_t, const uint8_t[64]);
} am_crypto_t;
typedef struct { bool exists, previous; char current[65], prior[65]; } am_pointer_t;
typedef enum {
    AM_BEFORE_STAGE, AM_AFTER_CHUNK, AM_AFTER_PAYLOAD_SYNC, AM_AFTER_VERIFY,
    AM_AFTER_PUBLISH, AM_BEFORE_ACTIVATE, AM_AFTER_ACTIVATE, AM_AFTER_ACTIVE_SYNC,
    AM_BEFORE_UNINSTALL, AM_AFTER_UNINSTALL, AM_AFTER_UNINSTALL_SYNC
} am_checkpoint_t;
typedef struct {
    void *context;
    am_result_t (*lock)(void *, bool);
    am_result_t (*read)(void *, const char *, uint8_t *, size_t, size_t *);
    am_result_t (*write)(void *, const char *, size_t, const uint8_t *, size_t);
    am_result_t (*mkdir)(void *, const char *);
    am_result_t (*sync)(void *, const char *);
    am_result_t (*move)(void *, const char *, const char *, bool);
    am_result_t (*remove)(void *, const char *);
    am_result_t (*versions)(void *, char[AM_VERSIONS_MAX][65], size_t *);
    am_result_t (*space)(void *, uint64_t *, uint64_t *);
    am_result_t (*checkpoint)(void *, am_checkpoint_t);
    am_result_t (*read_at)(void *,const char *,uint32_t,uint8_t *,size_t,size_t *); // <=4096B per call.
} am_io_t;
typedef struct {
    uint8_t manifest[AM_MANIFEST_MAX], signature[AM_SIGNATURE_MAX], payload[AM_PAYLOAD_SIZE];
    char versions[AM_VERSIONS_MAX][65];
} am_workspace_t; // Caller owns/allocates workspace; not an implicit home-task stack object.
typedef struct { const void *owner;uint32_t generation; } am_lease_t;
typedef struct {
    am_io_t io;am_crypto_t crypto;am_runtime_t runtime;am_workspace_t *work;
    const char *app_id; // Stable supported app identity; namespace is provided by the adapter.
    uint32_t reserve_bytes; // Includes qualified filesystem/COW and protected-data reserve.
    uint32_t allocation_unit; // Target4096, host0 uses exact logical-byte quota.
    am_lease_t *lease;uint32_t generation; // Private read lease holds the same nonrecursive store lock.
    void *(*doom_allocate)(size_t);void (*doom_free)(void *); // Temporary preflight only; small-app workspace stays unchanged.
} am_store_t;

am_result_t am_manifest_parse(am_bytes_t, am_metadata_t *);
am_result_t am_pointer_parse(am_bytes_t, am_pointer_t *);
size_t am_pointer_encode(const am_pointer_t *, uint8_t[192]);
bool am_token_valid(const char *);
am_result_t am_signature_raw(am_bytes_t, uint8_t[64]);
am_result_t am_package_integrity(const am_crypto_t *, am_package_t, am_metadata_t *, char[65]);
am_result_t am_package_receipt(const am_crypto_t *,am_bytes_t,am_bytes_t,am_metadata_t *,char[65]);
bool am_package_compatible(const am_metadata_t *, const am_runtime_t *);
am_result_t am_store_recover(am_store_t *);
am_result_t am_store_lookup(am_store_t *, am_metadata_t *); // DISABLED is installed, not corrupt/absent.
am_result_t am_store_install(am_store_t *, am_package_t);
am_result_t am_store_uninstall(am_store_t *); // App data is never an operation path.
// Caller provides a private aligned copy and retains lease through module execution AND deinit.
// Same task releases; install/recovery/unmount remain BUSY while the lease holds the lock.
am_result_t am_store_snapshot(am_store_t *,am_metadata_t *,uint8_t *,size_t,am_lease_t *);
am_result_t am_store_release(am_store_t *,am_lease_t *);
am_result_t am_store_read_lease(am_store_t *,am_lease_t *); // Private read capability; no package execution.
am_result_t am_store_doom_open(am_store_t *,am_metadata_t *,am_lease_t *,char[AM_PATH_MAX]);
