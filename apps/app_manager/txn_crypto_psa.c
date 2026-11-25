#include <string.h>
#include "psa/crypto.h"
#include "txn_crypto_psa.h"

static am_result_t digest(void *context,const uint8_t *data,size_t size,uint8_t out[32])
{
    (void)context;size_t written=0;
    if(psa_crypto_init()!=PSA_SUCCESS)return AM_IO;
    psa_status_t r=psa_hash_compute(PSA_ALG_SHA_256,data,size,out,32,&written);
    return r==PSA_SUCCESS && written==32?AM_OK:AM_IO;
}
static am_result_t verify(void *context,const uint8_t *data,size_t size,const uint8_t signature[64])
{
    am_psa_context_t *key=context;
    if(psa_crypto_init()!=PSA_SUCCESS)return AM_IO;
    psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes,PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes,256);
    psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attributes,PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_set_key_lifetime(&attributes,PSA_KEY_LIFETIME_VOLATILE);
    mbedtls_svc_key_id_t id=MBEDTLS_SVC_KEY_ID_INIT;
    psa_status_t r=psa_import_key(&attributes,key->public_key,sizeof key->public_key,&id);
    psa_reset_key_attributes(&attributes);
    if(r!=PSA_SUCCESS)return AM_IO;
    r=psa_verify_message(id,PSA_ALG_ECDSA(PSA_ALG_SHA_256),data,size,signature,64);
    psa_status_t released=psa_destroy_key(id);
    if(released!=PSA_SUCCESS)return AM_IO;
    return r==PSA_SUCCESS?AM_OK:r==PSA_ERROR_INVALID_SIGNATURE?AM_BAD_AUTH:AM_IO;
}
am_result_t am_psa_adapter(am_psa_context_t *context,const uint8_t *key,size_t size,am_crypto_t *out)
{
    if(!context || !key || size!=65 || key[0]!=4 || !out)return AM_BAD_AUTH;
    memcpy(context->public_key,key,65);
    *out=(am_crypto_t){context,digest,verify};
    return AM_OK;
}
am_result_t am_psa_sha256_stream(am_reader_t read,void *ctx,uint32_t size,uint8_t out[32])
{
    if(!read || !out || psa_crypto_init()!=PSA_SUCCESS)return AM_IO;
    psa_hash_operation_t hash=PSA_HASH_OPERATION_INIT;
    am_result_t r=psa_hash_setup(&hash,PSA_ALG_SHA_256)==PSA_SUCCESS?AM_OK:AM_IO;
    uint8_t chunk[512];uint32_t off=0;
    while(r==AM_OK && off<size) {
        size_t n=size-off;if(n>sizeof chunk)n=sizeof chunk;
        size_t got=0;r=read(ctx,off,chunk,n,&got);
        if(r==AM_OK && got!=n)r=AM_BAD_HASH;
        if(r==AM_OK && psa_hash_update(&hash,chunk,n)!=PSA_SUCCESS)r=AM_IO;
        off+=(uint32_t)n;
    }
    size_t got=0;
    if(r==AM_OK)r=read(ctx,size,chunk,1,&got);
    if(r==AM_OK && got)r=AM_BAD_HASH;
    size_t written=0;
    if(r==AM_OK && (psa_hash_finish(&hash,out,32,&written)!=PSA_SUCCESS || written!=32))r=AM_IO;
    if(psa_hash_abort(&hash)!=PSA_SUCCESS && r==AM_OK)r=AM_IO;
    return r;
}
