#pragma once
#include "../txn.h"
#include "../txn_vfs.h"
typedef struct {
    am_vfs_t vfs; // First member: common I/O context also identifies this host wrapper.
    int lock_fd;int fault,mode;bool write_full;
    am_result_t (*write_base)(void *,const char *,size_t,const uint8_t *,size_t);
} am_host_t;
am_result_t am_host_io(am_host_t *,const char *,uint64_t,am_io_t *);
am_result_t am_openssl_crypto(const char *,am_crypto_t *);
void am_openssl_close(am_crypto_t *);
am_result_t am_openssl_sha256_stream(am_reader_t,void *,uint32_t,uint8_t[32]);
