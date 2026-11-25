// Borrow the existing raw Doom WAD reservation; never download, erase or migrate it here.
#include <string.h>
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "spi_flash_mmap.h"
#include "stb_image.h"
#include "native_doom.h"
#include "txn_crypto_psa.h"
enum { WAD_ADDR=0x650000, WAD_Z_BYTES=1749852, WAD_Z_PAYLOAD=1749840, WAD_MAP_BYTES=0x1b0000 };
static const uint8_t compressed_hash[32]={
    0xd3,0x7d,0x78,0xd3,0x41,0xba,0xd6,0x78,0x86,0xd2,0x6a,0xa8,0x71,0xcb,0xd1,0x8a,
    0x9c,0xff,0xc2,0x0d,0x59,0xe6,0xe0,0xbf,0x03,0x4a,0x5d,0x0e,0x84,0x86,0xec,0xab};
static uint8_t *wad;
static uint32_t word(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static am_result_t read_mapped(void *ctx,uint32_t off,uint8_t *out,size_t cap,size_t *got)
{
    if(off>WAD_Z_BYTES || cap>4096)return AM_LIMIT;
    size_t n=WAD_Z_BYTES-off;if(n>cap)n=cap;memcpy(out,(const uint8_t *)ctx+off,n);*got=n;return AM_OK;
}
static int32_t begin(void *ctx,badge_doom_lease_t *out)
{
    (void)ctx;if(wad)return 0;
    const void *mapped=NULL;spi_flash_mmap_handle_t handle;
    if(spi_flash_mmap(WAD_ADDR,WAD_MAP_BYTES,SPI_FLASH_MMAP_DATA,&mapped,&handle)!=ESP_OK)return 0;
    const uint8_t *p=mapped;uint8_t hash[32];
    bool good=!memcmp(p,"DWZ1",4) && word(p+4)==BADGE_DOOM_WAD_BYTES && word(p+8)==WAD_Z_PAYLOAD;
    if(good)good=am_psa_sha256_stream(read_mapped,(void *)mapped,WAD_Z_BYTES,hash)==AM_OK && !memcmp(hash,compressed_hash,32);
    if(good) {
        wad=heap_caps_malloc(BADGE_DOOM_WAD_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        good=wad && stbi_zlib_decode_buffer((char *)wad,BADGE_DOOM_WAD_BYTES,(const char *)p+12,WAD_Z_PAYLOAD)==BADGE_DOOM_WAD_BYTES &&
            !memcmp(wad,"IWAD",4);
    }
    spi_flash_munmap(handle);
    if(!good){heap_caps_free(wad);wad=NULL;return 0;}
    *out=(badge_doom_lease_t){wad,BADGE_DOOM_WAD_BYTES,2};return 1;
}
static void finish(void *ctx) { (void)ctx;heap_caps_free(wad);wad=NULL; }
static void *resize(void *p,size_t n) { return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
am_result_t am_native_doom_target_launch(am_store_t *s,const am_lease_t *lease,const am_metadata_t *meta,const char *path)
{
    const am_native_doom_t io={.store=s,.lease=lease,.begin=begin,.resize=resize,.free=heap_caps_free,.finish=finish};
    return app_manager_launch_doom_source(s,lease,meta,path,&io);
}
