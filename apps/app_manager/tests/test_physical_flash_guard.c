// Reuse platform doubles, not the earlier guard scenario main or store suites.
#define main unused_guard_vectors
#include "test_littlefs_guard.c"
#undef main
int main(void)
{
    am_littlefs_t ctx;assert(am_littlefs_init(&ctx)==AM_OK);
    static am_workspace_t work;
    am_store_t store={.work=&work,.runtime={"esp32s3","6.0.2","2.1.0",1},
        .crypto={.sha256=hash,.verify_p256=verify}};
    am_layout_policy_t p=reset();p.qualified=false;flash_size=0x1000000;physical_result=ESP_FAIL;
    assert(am_littlefs_open(&ctx,&p,&store,NULL,NULL)==AM_DISABLED);
    assert(!physical_calls && !configured_calls && !reads && !mounts && !writes && !unmounts);
    puts("PASS unqualified default: zero physical/configured getter, read, mount or write calls");
    assert(am_littlefs_open(NULL,NULL,NULL,NULL,NULL)==AM_DISABLED);
    assert(!physical_calls && !configured_calls && !reads && !mounts && !writes);
    // The old configured getter incorrectly reaches the mount double here.
    p=reset();configured_size=0x800000;flash_size=0x1000000;
    assert(am_littlefs_open(&ctx,&p,&store,NULL,NULL)==AM_DISABLED);
    assert(physical_calls==1 && !configured_calls && reads==1 && !mounts && !writes && !unmounts);
    puts("PASS configured8MiB/physical16MiB: rejected before mount/write");
    p=reset();physical_result=ESP_FAIL;
    assert(am_littlefs_open(&ctx,&p,&store,NULL,NULL)==AM_IO);
    assert(physical_calls==1 && !configured_calls && !reads && !mounts && !writes && !unmounts);
    puts("PASS physical-size error: no table read, mount or write");
    p=reset();configured_size=0x800000;flash_size=0x400000;
    assert(am_littlefs_open(&ctx,&p,&store,NULL,NULL)==AM_DISABLED);
    assert(physical_calls==1 && !configured_calls && !mounts && !writes);
    puts("PASS configured8MiB/physical4MiB: rejected before mount/write");
    p=reset();
    assert(am_littlefs_open(&ctx,&p,&store,NULL,NULL)==AM_IO);
    assert(physical_calls==1 && !configured_calls && reads==1 && mounts==1 && !writes && !ctx.mounted);
    puts("PASS physical8MiB matching gate: reaches only injected failed mount, no real filesystem");
    return 0;
}
