#pragma once
#include "txn.h"
typedef struct { uint8_t public_key[65]; } am_psa_context_t;
// Caller supplies a trusted SEC1 P-256 public key; this code embeds no key or mount.
am_result_t am_psa_adapter(am_psa_context_t *,const uint8_t *,size_t,am_crypto_t *);
am_result_t am_psa_sha256_stream(am_reader_t,void *,uint32_t,uint8_t[32]);
