#include <string.h>
#include "txn_geometry.h"

static bool bounds(uint64_t a,uint64_t size,uint64_t flash,uint64_t *end)
{
    if(!size || size>UINT64_MAX-a)return false;
    *end=a+size;return *end<=flash && !(a%4096) && !(size%4096);
}
static bool overlap(uint64_t a,uint64_t b,uint64_t c,uint64_t d)
{ return a<d && c<b; }
am_result_t am_geometry_check(const am_layout_policy_t *policy,const am_partition_view_t *parts,
                              size_t count,uint64_t flash,const uint8_t actual[32])
{
    uint64_t end;uint8_t digest=0,legacy=0;
    if(!policy || !policy->qualified)return AM_DISABLED;
    for(unsigned i=0;i<32;i++) {
        digest|=policy->table_sector_sha256[i];legacy|=policy->legacy_table_sector_sha256[i];
    }
    if(!digest || !actual || !parts || !count || count>AM_PARTITIONS_MAX ||
       flash!=0x800000 || policy->flash_size!=flash || policy->reserve_bytes<4096 ||
       policy->reserve_bytes>=policy->size ||
       (memcmp(actual,policy->table_sector_sha256,32) &&
        (!legacy || memcmp(actual,policy->legacy_table_sector_sha256,32))) ||
       !bounds(policy->address,policy->size,flash,&end))return AM_DISABLED;
    const uint64_t protected[][2]={{0,0x20000},{0x620000,0x630000},
                                   {0x630000,0x650000},{0x650000,0x800000}};
    for(unsigned i=0;i<4;i++)
        if(overlap(policy->address,end,protected[i][0],protected[i][1]))return AM_DISABLED;
    unsigned found=0;
    for(size_t i=0;i<count;i++) {
        uint64_t part_end;
        if(!bounds(parts[i].address,parts[i].size,flash,&part_end))return AM_DISABLED;
        if(!memcmp(parts[i].label,"appstore",9)) {
            found++;
            if(parts[i].type!=1 || parts[i].subtype!=0x83 || parts[i].encrypted || parts[i].readonly ||
               parts[i].address!=policy->address || parts[i].size!=policy->size)return AM_DISABLED;
        } else if(overlap(policy->address,end,parts[i].address,part_end))return AM_DISABLED;
        for(size_t j=0;j<i;j++) {
            uint64_t other_end;
            if(!bounds(parts[j].address,parts[j].size,flash,&other_end) ||
               overlap(parts[i].address,part_end,parts[j].address,other_end))return AM_DISABLED;
        }
    }
    return found==1?AM_OK:AM_DISABLED;
}
