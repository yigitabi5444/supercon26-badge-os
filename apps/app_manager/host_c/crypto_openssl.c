#include <stdio.h>
#include <string.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include "adapters.h"

static am_result_t digest(void *context,const uint8_t *data,size_t size,uint8_t out[32])
{
    (void)context;size_t written=0;
    return EVP_Q_digest(NULL,"SHA256",NULL,data,size,out,&written)==1 && written==32?AM_OK:AM_IO;
}
static size_t integer(uint8_t *out,const uint8_t scalar[32])
{
    size_t first=0;while(first<31 && !scalar[first])first++;
    size_t n=32-first,pad=(scalar[first]&128)?1:0;
    out[0]=2;out[1]=(uint8_t)(n+pad);
    if(pad)out[2]=0;
    memcpy(out+2+pad,scalar+first,n);return n+pad+2;
}
static am_result_t verify(void *context,const uint8_t *data,size_t size,const uint8_t raw[64])
{
    uint8_t der[72];size_t n=integer(der+2,raw);n+=integer(der+2+n,raw+32);
    der[0]=0x30;der[1]=(uint8_t)n;
    EVP_MD_CTX *md=EVP_MD_CTX_new();if(!md)return AM_IO;
    int r=EVP_DigestVerifyInit(md,NULL,EVP_sha256(),NULL,context);
    if(r==1)r=EVP_DigestVerify(md,der,n+2,data,size);
    EVP_MD_CTX_free(md);
    return r==1?AM_OK:r==0?AM_BAD_AUTH:AM_IO;
}
am_result_t am_openssl_crypto(const char *path,am_crypto_t *out)
{
    FILE *file=fopen(path,"r");if(!file)return AM_IO;
    EVP_PKEY *key=PEM_read_PUBKEY(file,NULL,NULL,NULL);fclose(file);
    char group[32];size_t size=0;
    if(!key || !EVP_PKEY_is_a(key,"EC") || EVP_PKEY_get_bits(key)!=256 ||
       EVP_PKEY_get_utf8_string_param(key,OSSL_PKEY_PARAM_GROUP_NAME,group,sizeof group,&size)!=1 ||
       strcmp(group,"prime256v1")) {EVP_PKEY_free(key);return AM_BAD_AUTH;}
    *out=(am_crypto_t){key,digest,verify};return AM_OK;
}
void am_openssl_close(am_crypto_t *crypto)
{ EVP_PKEY_free(crypto->context);memset(crypto,0,sizeof *crypto); }
am_result_t am_openssl_sha256_stream(am_reader_t read,void *ctx,uint32_t size,uint8_t out[32])
{
    if(!read || !out)return AM_IO;
    EVP_MD_CTX *md=EVP_MD_CTX_new();if(!md)return AM_IO;
    am_result_t r=EVP_DigestInit_ex(md,EVP_sha256(),NULL)==1?AM_OK:AM_IO;
    uint8_t chunk[512];uint32_t off=0;
    while(r==AM_OK && off<size) {
        size_t n=size-off;if(n>sizeof chunk)n=sizeof chunk;
        size_t got=0;r=read(ctx,off,chunk,n,&got);
        if(r==AM_OK && got!=n)r=AM_BAD_HASH;
        if(r==AM_OK && EVP_DigestUpdate(md,chunk,n)!=1)r=AM_IO;
        off+=(uint32_t)n;
    }
    size_t got=0;
    if(r==AM_OK)r=read(ctx,size,chunk,1,&got);
    if(r==AM_OK && got)r=AM_BAD_HASH;
    unsigned written=0;
    if(r==AM_OK && (EVP_DigestFinal_ex(md,out,&written)!=1 || written!=32))r=AM_IO;
    EVP_MD_CTX_free(md);return r;
}
