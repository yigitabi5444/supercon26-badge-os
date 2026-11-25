#pragma once
#include "txn.h"
enum { AM_URL_MAX = 2048, AM_TLS_CA_MAX = 8192 };
typedef struct {
    const char *manifest_url,*signature_url,*payload_url,*redirect_host;
    const uint8_t *public_key;size_t key_size;
    const char *local_authority,*tls_ca_pem; // Optional pair: canonical IPv4:8443 and its private CA.
} am_source_config_t; // Immutable, caller-owned configuration; NULL/zero disables fetching.
typedef struct {
    uint8_t manifest[AM_MANIFEST_MAX],signature[AM_SIGNATURE_MAX],payload[AM_PAYLOAD_SIZE];
} am_source_bytes_t;
typedef struct {
    const am_source_config_t *config;am_source_bytes_t *bytes;
    am_package_t package;bool valid;
    void (*progress)(const char *);
    const char *app_id; // Curated selected identity; NULL preserves the original SpaceInv binding.
    uint8_t *doom_payload; // Temporary Doom-only acquisition; discarded when the action ends.
} am_source_t; // One owning app task; published spans immutable until explicit fetch/discard.
typedef am_result_t (*am_source_get_t)(void *,const char *,uint8_t *,size_t,size_t *);
bool am_source_config_valid(const am_source_config_t *);
bool am_source_url_valid(const char *,const char *host); // Public HTTPS/DNS; no credentials/port/fragment.
bool am_source_url_allowed(const am_source_config_t *,const char *); // Local exact authority, otherwise public policy.
am_result_t am_source_fetch(am_source_t *,const am_crypto_t *,const am_runtime_t *,
                            am_source_get_t,void *,bool (*)(void *),void *);
am_result_t am_source_cached(const am_source_t *,am_package_t *); // No I/O.
extern const am_source_config_t am_app_source_config;
extern const am_source_config_t am_rocks_source_config;
extern const am_source_config_t am_codex_source_config;
extern const am_source_config_t am_hackaday_source_config;
extern const am_source_config_t am_messages_source_config;
extern const am_source_config_t am_doom_source_config;
