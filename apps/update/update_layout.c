// Legacy-only preservation gate. Store/unknown layouts require a qualified host route.
#include <stdint.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_flash.h"
#include "psa/crypto.h"
#include "update_layout.h"

typedef bool (*read_fn)(void *,uint32_t,uint8_t *,size_t);
static bool legacy(read_fn read,void *context,uint32_t flash_size)
{
    // Fingerprints cover the entire physical sector, including checksum/flags/tail.
    static const uint8_t known[][32]={
    {0xf4,0x6d,0x81,0xd2,0x05,0xf0,0xe3,0x6c,0x13,0x3d,0xee,0x79,0x72,0x0b,0x48,0x3a,0xf6,0xd3,0x24,0x14,0x3b,0x93,0x31,0xd3,0x46,0x0f,0x74,0x69,0x45,0xe2,0x98,0x30},
    {0xd4,0x97,0x40,0xbc,0xc4,0x43,0x44,0x6f,0x52,0x9b,0x7c,0x9f,0x48,0xdf,0xe6,0xa5,0xd5,0x15,0xe8,0x3e,0x97,0x5b,0x17,0x74,0xc0,0xdc,0x0e,0x8a,0xc5,0x0f,0xe0,0x9c}
    };
    if(flash_size!=0x800000 || CONFIG_PARTITION_TABLE_OFFSET!=0x8000 ||
       psa_crypto_init()!=PSA_SUCCESS)return false;
    psa_hash_operation_t hash=PSA_HASH_OPERATION_INIT;
    uint8_t block[128],digest[32];size_t size=0;
    bool ok=psa_hash_setup(&hash,PSA_ALG_SHA_256)==PSA_SUCCESS;
    for(uint32_t off=0;ok && off<4096;off+=sizeof block)
        ok=read(context,0x8000+off,block,sizeof block) &&
           psa_hash_update(&hash,block,sizeof block)==PSA_SUCCESS;
    if(ok)ok=psa_hash_finish(&hash,digest,sizeof digest,&size)==PSA_SUCCESS && size==32;
    bool allowed=ok && (!memcmp(digest,known[0],32) || !memcmp(digest,known[1],32));
    return psa_hash_abort(&hash)==PSA_SUCCESS && allowed;
}
static bool local_read(void *context,uint32_t address,uint8_t *out,size_t size)
{ (void)context;return esp_flash_read(NULL,out,address,size)==ESP_OK; }
bool update_layout_local_legacy(void)
{
    uint32_t size=0,physical=0;
    return esp_flash_get_size(NULL,&size)==ESP_OK && size==0x800000 &&
           esp_flash_get_physical_size(NULL,&physical)==ESP_OK && legacy(local_read,NULL,physical);
}
static bool peer_read(void *context,uint32_t address,uint8_t *out,size_t size)
{ return esp_loader_flash_read(context,out,address,(uint32_t)size)==ESP_LOADER_SUCCESS; }
bool update_layout_peer_legacy(esp_loader_t *loader)
{
    uint32_t size=0;
    return loader && esp_loader_get_target(loader)==ESP32S3_CHIP &&
           esp_loader_flash_detect_size(loader,&size)==ESP_LOADER_SUCCESS &&
           legacy(peer_read,loader,size);
}
