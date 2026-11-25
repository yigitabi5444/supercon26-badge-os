// Compile the actual target adapter against count-only platform doubles.
// No filesystem is mounted and no flash write/format primitive is provided.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_partition.h"
#include "esp_littlefs.h"
#include "../txn_littlefs.h"

static esp_flash_t chip,external;
esp_flash_t *esp_flash_default_chip=&chip;
static esp_partition_t parts[33];static size_t count;
struct iterator { size_t index; };static struct iterator cursor;
static unsigned reads,mounts,writes,unmounts,checks;
static esp_err_t read_result,mount_result,physical_result;
static am_result_t io_result;
static uint32_t flash_size,configured_size;static bool bad_hash;
static unsigned physical_calls,configured_calls;
static uint8_t table[4096];
esp_err_t esp_flash_get_size(esp_flash_t *c,uint32_t *size)
{ assert(!c);configured_calls++;*size=configured_size;return ESP_OK; }
esp_err_t esp_flash_get_physical_size(esp_flash_t *c,uint32_t *size)
{ assert(!c);physical_calls++;if(physical_result==ESP_OK)*size=flash_size;return physical_result; }
esp_err_t esp_flash_read(esp_flash_t *c,void *out,uint32_t address,uint32_t size)
{ assert(!c && address==0x8000 && size==4096);reads++;memcpy(out,table,4096);return read_result; }
esp_err_t esp_partition_find_err(int type,int subtype,const char *label,esp_partition_iterator_t *it)
{ assert(type==255 && subtype==255 && !label);cursor.index=0;*it=count?&cursor:NULL;return count?ESP_OK:ESP_FAIL; }
const esp_partition_t *esp_partition_get(esp_partition_iterator_t it) { return &parts[it->index]; }
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t it)
{ return ++it->index==count?NULL:it; }
void esp_partition_iterator_release(esp_partition_iterator_t it) { assert(it==&cursor); }
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *c)
{
    assert(!strcmp(c->base_path,"/appstore") && !strcmp(c->partition_label,"appstore"));
    assert(!c->format_if_mount_failed && !c->grow_on_mount);mounts++;return mount_result;
}
esp_err_t esp_vfs_littlefs_unregister(const char *label)
{ assert(!strcmp(label,"appstore"));unmounts++;return ESP_OK; }
esp_err_t esp_littlefs_info(const char *label,size_t *total,size_t *used)
{ assert(!strcmp(label,"appstore"));*total=0x100000;*used=0;return ESP_OK; }
am_result_t am_vfs_io(am_vfs_t *v,const char *root,uint64_t quota,am_io_t *io)
{ writes++;memset(io,0,sizeof *io);io->context=v;strcpy(v->root,root);v->quota=quota;return io_result; }
am_result_t am_vfs_readonly_io(am_vfs_t *v,const char *root,uint64_t quota,am_io_t *io)
{(void)v;(void)root;(void)quota;(void)io;assert(!"Not used by the original write-path guard cases");return AM_DISABLED;}
static bool cancel(void *context) { return *(bool *)context; }
static am_result_t hash(void *ctx,const uint8_t *bytes,size_t size,uint8_t out[32])
{ (void)ctx;assert(size==4096 && !memcmp(bytes,table,size));memset(out,0,32);out[0]=bad_hash?2:1;return AM_OK; }
static am_result_t verify(void *ctx,const uint8_t *bytes,size_t size,const uint8_t sig[64])
{ (void)ctx;(void)bytes;(void)size;(void)sig;assert(0);return AM_BAD_AUTH; }
static am_layout_policy_t reset(void)
{
    reads=mounts=writes=unmounts=0;read_result=ESP_OK;mount_result=ESP_FAIL;io_result=AM_IO;
    physical_calls=configured_calls=0;physical_result=ESP_OK;
    flash_size=configured_size=0x800000;bad_hash=false;count=3;memset(parts,0,sizeof parts);memset(table,0x55,sizeof table);
    parts[0]=(esp_partition_t){.type=1,.address=0x9000,.size=0x5000,.label="nvs"};
    parts[1]=(esp_partition_t){.address=0x20000,.size=0x500000,.label="ota"};
    parts[2]=(esp_partition_t){.type=1,.subtype=0x83,.address=0x520000,.size=0x100000,.label="appstore"};
    for(unsigned i=0;i<3;i++){parts[i].flash_chip=&chip;parts[i].erase_size=4096;}
    return (am_layout_policy_t){.qualified=true,.address=0x520000,.size=0x100000,
        .flash_size=0x800000,.reserve_bytes=4096,.table_sector_sha256={1}};
}
static void rejected(am_littlefs_t *ctx,am_store_t *store,am_layout_policy_t *policy)
{
    uint8_t before[4096];memcpy(before,table,sizeof before);
    assert(am_littlefs_open(ctx,policy,store,NULL,NULL)!=AM_OK);
    assert(!mounts && !writes && !unmounts && !memcmp(before,table,sizeof before));checks++;
}
int main(void)
{
    am_littlefs_t ctx;assert(am_littlefs_init(&ctx)==AM_OK);
    static am_workspace_t work;
    am_store_t store={.app_id="aliens",.work=&work,.runtime={"esp32s3","6.0.2","2.1.0",1},
        .crypto={.sha256=hash,.verify_p256=verify}};
    am_layout_policy_t p=reset();p.qualified=false;rejected(&ctx,&store,&p);assert(!reads);
    assert(am_littlefs_open(NULL,NULL,NULL,NULL,NULL)==AM_DISABLED);checks++;
    p=reset();count=0;rejected(&ctx,&store,&p);
    p=reset();count=2;rejected(&ctx,&store,&p); // No appstore on the actual current layout.
    p=reset();parts[3]=parts[2];count=4;rejected(&ctx,&store,&p);
    p=reset();parts[2].type=0;rejected(&ctx,&store,&p);
    p=reset();parts[2].subtype=0x81;rejected(&ctx,&store,&p);
    p=reset();parts[2].encrypted=true;rejected(&ctx,&store,&p);
    p=reset();parts[2].readonly=true;rejected(&ctx,&store,&p);
    p=reset();parts[2].flash_chip=&external;rejected(&ctx,&store,&p);
    p=reset();parts[2].erase_size=8192;rejected(&ctx,&store,&p);
    p=reset();parts[2].size=0;rejected(&ctx,&store,&p);
    p=reset();parts[2].size--;rejected(&ctx,&store,&p);
    p=reset();parts[2].address++;rejected(&ctx,&store,&p);
    p=reset();p.address++;rejected(&ctx,&store,&p);
    p=reset();p.address=UINT64_MAX-4095;rejected(&ctx,&store,&p);
    p=reset();p.size=UINT64_MAX;rejected(&ctx,&store,&p);
    p=reset();p.reserve_bytes=0;rejected(&ctx,&store,&p);
    p=reset();p.table_sector_sha256[0]=0;rejected(&ctx,&store,&p);
    p=reset();bad_hash=true;rejected(&ctx,&store,&p);
    p=reset();read_result=ESP_FAIL;rejected(&ctx,&store,&p);
    p=reset();flash_size=0x400000;rejected(&ctx,&store,&p);
    p=reset();parts[1].size+=4096;rejected(&ctx,&store,&p); // Actual OTA crosses candidate.
    p=reset();parts[0].address=0x520000;rejected(&ctx,&store,&p); // Actual NVS crosses candidate.
    const uint32_t addresses[]={0,0x1f000,0x620000,0x630000,0x650000,0x7ff000,0x800000};
    for(unsigned i=0;i<7;i++) {
        p=reset();parts[2].address=addresses[i];p.address=addresses[i];
        rejected(&ctx,&store,&p);
    }
    p=reset();
    for(unsigned i=3;i<33;i++)parts[i]=parts[2];
    count=33;rejected(&ctx,&store,&p);
    // Synthetic exact adjacency passes the gate; failed mount never creates files.
    p=reset();assert(am_littlefs_open(&ctx,&p,&store,NULL,NULL)==AM_IO);
    assert(mounts==1 && !writes && !ctx.mounted);checks++;
    // Even an injected successful register is cleaned up if directory initialization fails.
    p=reset();mount_result=ESP_OK;
    assert(am_littlefs_open(&ctx,&p,&store,NULL,NULL)==AM_IO);
    assert(mounts==1 && writes==1 && unmounts==1 && !ctx.mounted);checks++;
    // Same nonrecursive mutex guards the logical module lease and unmount.
    assert(xSemaphoreTake(ctx.mutex,0)==pdTRUE);assert(am_littlefs_close(&ctx)==AM_BUSY);
    assert(xSemaphoreGive(ctx.mutex)==pdTRUE);assert(am_littlefs_close(&ctx)==AM_OK);checks++;
    // Exercise only adapter lock/cancel policy with an injected VFS success, no file calls.
    p=reset();mount_result=ESP_OK;io_result=AM_OK;bool pending=true;
    assert(am_littlefs_open(&ctx,&p,&store,cancel,&pending)==AM_OK);
    assert(store.io.lock(store.io.context,true)==AM_OK);
    assert(store.io.checkpoint(store.io.context,AM_BEFORE_ACTIVATE)==AM_CANCELLED);
    assert(store.io.checkpoint(store.io.context,AM_AFTER_ACTIVATE)==AM_OK);
    assert(store.io.checkpoint(store.io.context,AM_AFTER_ACTIVE_SYNC)==AM_OK);
    assert(am_littlefs_close(&ctx)==AM_BUSY);
    assert(store.io.lock(store.io.context,false)==AM_OK);
    assert(store.io.lock(store.io.context,true)==AM_OK);
    assert(store.io.checkpoint(store.io.context,AM_BEFORE_UNINSTALL)==AM_CANCELLED);
    assert(store.io.checkpoint(store.io.context,AM_AFTER_UNINSTALL)==AM_OK);
    assert(store.io.lock(store.io.context,false)==AM_OK);
    assert(am_littlefs_close(&ctx)==AM_OK);
    assert(store.io.lock(store.io.context,true)==AM_DISABLED);checks++;
    printf("%u target-guard scenarios passed (platform doubles; no real mount/write)\n",checks);
    return 0;
}
