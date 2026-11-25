#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sdkconfig.h"
#include "esp_flash.h"
#include "esp_partition.h"
#include "esp_littlefs.h"
#include "txn_littlefs.h"
#include "managed_apps.h"

_Static_assert(CONFIG_LITTLEFS_OBJ_NAME_LEN>=80,"Apply sdkconfig.store.defaults: digest names need65B");
_Static_assert(CONFIG_LITTLEFS_MAX_PARTITIONS==1,"First managed store qualifies one volume only");
#if defined(CONFIG_LITTLEFS_USE_ONLY_HASH) || defined(CONFIG_LITTLEFS_SPIFFS_COMPAT)
#error "Managed store requires fstat and explicit directories"
#endif

static am_result_t lock_store(void *context,bool acquire)
{
    am_littlefs_t *h=context;
    if(acquire) {
        if(xSemaphoreTake(h->mutex,0)!=pdTRUE)return AM_BUSY;
        if(!h->mounted){xSemaphoreGive(h->mutex);return AM_DISABLED;}
        h->committed=false;return AM_OK;
    }
    return xSemaphoreGive(h->mutex)==pdTRUE?AM_OK:AM_IO;
}
static am_result_t checkpoint(void *context,am_checkpoint_t point)
{
    am_littlefs_t *h=context;
    if(point==AM_AFTER_ACTIVATE || point==AM_AFTER_UNINSTALL)h->committed=true;
    // Once the pointer commit happened, finish durability/readback before honoring Home.
    return !h->committed && h->cancel && h->cancel(h->cancel_context)?AM_CANCELLED:AM_OK;
}
static am_result_t space(void *context,uint64_t *used,uint64_t *total)
{
    (void)context;size_t t=0,u=0;
    if(esp_littlefs_info("appstore",&t,&u)!=ESP_OK || u>t)return AM_IO;
    *total=t;*used=u;return AM_OK;
}
static am_result_t sync_path(void *context,const char *relative)
{
    am_littlefs_t *h=context;char p[AM_VFS_PATH_MAX];struct stat st;
    if(strlen(relative)>=AM_PATH_MAX || strstr(relative,"..") || relative[0]=='/')return AM_LIMIT;
    int n=snprintf(p,sizeof p,"%s/%s",h->vfs.root,relative);
    if(n<0 || (size_t)n>=sizeof p || stat(p,&st))return AM_IO;
    // This port has no directory-FD fsync. mkdir/rename/remove commit synchronously
    // through LittleFS; do not pretend a failed directory fsync is a barrier.
    if(S_ISDIR(st.st_mode))return AM_OK;
    if(!S_ISREG(st.st_mode))return AM_IO;
    int fd=open(p,O_RDWR);if(fd<0)return AM_IO;
    int synced=fsync(fd),closed=close(fd);return synced || closed?AM_IO:AM_OK;
}
static am_result_t probe(const am_layout_policy_t *policy,am_store_t *store)
{
    // Only this physical 4KiB table-sector digest is accepted, not a 3072B CSV binary hash.
    if(CONFIG_PARTITION_TABLE_OFFSET!=0x8000)return AM_DISABLED;
    uint32_t flash=0;uint8_t digest[32];
    if(esp_flash_get_physical_size(NULL,&flash)!=ESP_OK ||
       esp_flash_read(NULL,store->work->manifest,0x8000,4096)!=ESP_OK)return AM_IO;
    am_result_t r=store->crypto.sha256(store->crypto.context,store->work->manifest,4096,digest);
    if(r!=AM_OK)return r;
    am_partition_view_t parts[AM_PARTITIONS_MAX];size_t count=0;
    esp_partition_iterator_t it=NULL;
    if(esp_partition_find_err(ESP_PARTITION_TYPE_ANY,ESP_PARTITION_SUBTYPE_ANY,NULL,&it)!=ESP_OK)
        return AM_DISABLED;
    while(it) {
        const esp_partition_t *p=esp_partition_get(it);
        if(!p || p->flash_chip!=esp_flash_default_chip || p->erase_size!=4096 ||
           count==AM_PARTITIONS_MAX) {
            esp_partition_iterator_release(it);return AM_DISABLED;
        }
        parts[count]=(am_partition_view_t){.address=p->address,.size=p->size,
            .type=p->type,.subtype=p->subtype,.encrypted=p->encrypted,.readonly=p->readonly};
        memcpy(parts[count++].label,p->label,17);
        it=esp_partition_next(it); // End-of-list releases iterator, including on NULL.
    }
    return am_geometry_check(policy,parts,count,flash,digest);
}
static am_result_t bind_namespace(am_littlefs_t *h,am_store_t *store,const char *root,uint64_t size)
{
    am_vfs_t next;am_io_t io;
    am_result_t r=h->readonly?am_vfs_readonly_io(&next,root,size,&io):am_vfs_io(&next,root,size,&io);
    if(r==AM_OK) {
        h->vfs=next;io.context=h;
        io.lock=lock_store;if(!h->readonly)io.sync=sync_path;
        io.space=space;io.checkpoint=checkpoint;
        store->io=io;
    }
    return r;
}
am_result_t am_littlefs_init(am_littlefs_t *h)
{
    if(!h)return AM_IO;
    memset(h,0,sizeof *h);h->mutex=xSemaphoreCreateMutexStatic(&h->mutex_storage);
    return h->mutex?AM_OK:AM_IO;
}
static am_result_t open_mode(am_littlefs_t *h,const am_layout_policy_t *policy,am_store_t *store,
                             bool (*cancel)(void *),void *cancel_context,bool readonly)
{
    if(!policy || !policy->qualified)return AM_DISABLED;
    if(!h || !h->mutex || !store || !store->work || !store->crypto.sha256 ||
       !store->crypto.verify_p256 || !store->runtime.target || !store->runtime.sdk ||
       !store->runtime.loader)return AM_IO;
    const char *root=readonly?"/appstore":am_managed_namespace(store->app_id);
    if(!root)return AM_DISABLED;
    if(xSemaphoreTake(h->mutex,0)!=pdTRUE)return AM_BUSY;
    am_result_t r=h->mounted?AM_BUSY:probe(policy,store);
    if(r==AM_OK) {
        const esp_vfs_littlefs_conf_t config={.base_path="/appstore",.partition_label="appstore",
            .format_if_mount_failed=false,.grow_on_mount=false,.read_only=readonly};
        if(esp_vfs_littlefs_register(&config)!=ESP_OK)r=AM_IO;
        else {
            h->mounted=true;h->readonly=readonly;
            r=bind_namespace(h,store,root,policy->size);
            if(r==AM_OK) {
                h->cancel=cancel;h->cancel_context=cancel_context;
                store->reserve_bytes=policy->reserve_bytes;store->allocation_unit=4096;
            } else if(esp_vfs_littlefs_unregister("appstore")==ESP_OK)h->mounted=false;
        }
    }
    if(xSemaphoreGive(h->mutex)!=pdTRUE)r=AM_IO;
    return r;
}
am_result_t am_littlefs_open(am_littlefs_t *h,const am_layout_policy_t *p,am_store_t *s,bool (*cancel)(void *),void *ctx)
{return open_mode(h,p,s,cancel,ctx,false);}
am_result_t am_littlefs_open_readonly(am_littlefs_t *h,const am_layout_policy_t *p,am_store_t *s)
{return open_mode(h,p,s,NULL,NULL,true);}
am_result_t am_littlefs_select(am_littlefs_t *h,am_store_t *store,const char *id,bool create)
{
    const char *root=am_managed_namespace(id);
    if(!root)return AM_DISABLED;
    if(!h || !h->mutex || !store)return AM_IO;
    if(h->readonly && create)return AM_DISABLED;
    if(store->lease && store->lease->owner)return AM_BUSY;
    if(xSemaphoreTake(h->mutex,0)!=pdTRUE)return AM_BUSY;
    am_result_t r=AM_DISABLED;
    if(h->mounted) {
        struct stat st;
        if(!create && stat(root,&st))r=errno==ENOENT?AM_NOT_FOUND:AM_IO;
        else r=bind_namespace(h,store,root,h->vfs.quota);
        if(r==AM_OK)store->app_id=id;
    }
    if(xSemaphoreGive(h->mutex)!=pdTRUE)r=AM_IO;
    return r;
}
am_result_t am_littlefs_close(am_littlefs_t *h)
{
    if(!h || !h->mutex)return AM_IO;
    if(xSemaphoreTake(h->mutex,0)!=pdTRUE)return AM_BUSY;
    am_result_t r=AM_OK;
    if(h->mounted) {
        if(esp_vfs_littlefs_unregister("appstore")!=ESP_OK)r=AM_IO;
        else h->mounted=h->readonly=false;
    }
    if(xSemaphoreGive(h->mutex)!=pdTRUE)r=AM_IO;
    return r;
}
