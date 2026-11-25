#include <string.h>
#include "txn.h"
#include "native_elf.h"

// Canonical DER P-256 signatures map to PSA's public fixed-width r || s encoding.
am_result_t am_signature_raw(am_bytes_t signature, uint8_t out[64])
{
    if(signature.size<8 || signature.size>72 || !signature.data)return AM_BAD_AUTH;
    const uint8_t *p=signature.data,*end=p+signature.size;
    if(*p++!=0x30 || *p++!=signature.size-2)return AM_BAD_AUTH;
    memset(out,0,64);
    for(unsigned i=0;i<2;i++) {
        if(end-p<2 || *p++!=0x02)return AM_BAD_AUTH;
        size_t n=*p++;
        if(!n || n>33 || (size_t)(end-p)<n || (*p&0x80))return AM_BAD_AUTH;
        if(n>1 && *p==0 && !(p[1]&0x80))return AM_BAD_AUTH;
        if(n==33){if(*p++)return AM_BAD_AUTH;n--;}
        memcpy(out+i*32+32-n,p,n);p+=n;
    }
    return p==end?AM_OK:AM_BAD_AUTH;
}
static void hex(const uint8_t bytes[32],char out[65])
{
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++){out[i*2]=digits[bytes[i]>>4];out[i*2+1]=digits[bytes[i]&15];}
    out[64]=0;
}
am_result_t am_package_receipt(const am_crypto_t *crypto,am_bytes_t manifest,am_bytes_t signature,
                                am_metadata_t *meta,char token[65])
{
    if(!crypto || !crypto->sha256 || !crypto->verify_p256 || !meta || !token)return AM_IO;
    if(!manifest.data || !manifest.size || manifest.size>AM_MANIFEST_MAX || signature.size>AM_SIGNATURE_MAX)return AM_BAD_MANIFEST;
    uint8_t raw[64],digest[32];am_result_t r=am_signature_raw(signature,raw);
    if(r==AM_OK)r=crypto->verify_p256(crypto->context,manifest.data,manifest.size,raw);
    if(r==AM_OK)r=am_manifest_parse(manifest,meta);
    if(r==AM_OK)r=crypto->sha256(crypto->context,manifest.data,manifest.size,digest);
    if(r==AM_OK)hex(digest,token);
    return r;
}
am_result_t am_package_integrity(const am_crypto_t *crypto,am_package_t p,am_metadata_t *meta,char token[65])
{
    am_result_t r=am_package_receipt(crypto,p.manifest,p.signature,meta,token);
    if(r!=AM_OK)return r;
    // Only the authenticated Doom identity receives its measured larger budget.
    size_t cap=!strcmp(meta->id,"doom")?AM_DOOM_PAYLOAD_MAX:AM_PAYLOAD_MAX;
    if(!p.payload.data || p.payload.size<52 || p.payload.size>cap)return AM_BAD_MANIFEST;
    uint8_t digest[32];char actual[65];
    r=crypto->sha256(crypto->context,p.payload.data,p.payload.size,digest);
    if(r!=AM_OK)return r;
    hex(digest,actual);
    if(meta->size!=p.payload.size || strcmp(meta->hash,actual))return AM_BAD_HASH;
    return am_native_elf_check(p.payload,meta->id,meta->version,meta->abi);
}
bool am_package_compatible(const am_metadata_t *m,const am_runtime_t *runtime)
{
    // The selected native ABI/toolchain; no general SDK compatibility claim.
    return m->abi==2 && runtime->abi==2 && !strcmp(m->target,"esp32s3") &&
        !strcmp(runtime->target,m->target) && !strcmp(m->sdk,"6.0.2") && !strcmp(runtime->sdk,m->sdk) &&
        !strcmp(m->loader,"2.1.0") && !strcmp(runtime->loader,m->loader);
}
