#pragma once
#include <stdbool.h>
#include <stdint.h>
// Standard-port public media only. Caller pins a resolved public IPv4 address,
// while retaining this hostname for HTTP Host and TLS SNI/certificate matching.
bool am_had_url(const char *,bool feed,char host[254],const char **path,bool *tls);
bool am_had_public_ipv4(uint32_t host_order);
bool am_had_redirect(const char *,const char *,bool feed,char out[400]);
