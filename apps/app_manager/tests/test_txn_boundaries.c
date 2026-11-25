#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../txn_geometry.h"

static unsigned checks;
#define CHECK(e) do { assert(e);checks++; } while(0)
static void json(void)
{
    const char valid[]="{\"schema\":1,\"id\":\"counter\",\"version\":\"999999.999999.999999\","
        "\"target\":\"esp32s3\",\"abi\":1,\"sdk\":\"6.0.2\",\"loader\":\"2.1.0\",\"bytes\":1288,"
        "\"sha256\":\"1c029eff8aa4a8d44e7dc65a3ad1ba28961fff200c37223b8d5fc14eb3d8c96d\"}";
    am_metadata_t meta;am_bytes_t input={(const uint8_t *)valid,strlen(valid)};
    CHECK(am_manifest_parse(input,&meta)==AM_OK);
    CHECK(meta.parts[2]==999999);
    for(size_t n=0;n<input.size;n++)
        CHECK(am_manifest_parse((am_bytes_t){input.data,n},&meta)==AM_BAD_MANIFEST);
    char mutated[512];
    const char *from[]={"\"schema\":1","\"abi\":1","\"bytes\":1288","\"sdk\":\"6.0.2\"",
        "\"target\":\"esp32s3\"","999999.999999.999999","\"id\":\"counter\"","\"loader\":\"2.1.0\""};
    const char *to[]={"\"schema\":true","\"abi\":4294967296","\"bytes\":01288","\"abi\":1",
        "\"target\":\"\"","1000000.0.0","\"id\":\"count\\u0065r\"","\"loader\":null"};
    for(unsigned i=0;i<8;i++) {
        const char *at=strstr(valid,from[i]);assert(at);size_t prefix=(size_t)(at-valid);
        snprintf(mutated,sizeof mutated,"%.*s%s%s",(int)prefix,valid,to[i],at+strlen(from[i]));
        CHECK(am_manifest_parse((am_bytes_t){(uint8_t *)mutated,strlen(mutated)},&meta)==AM_BAD_MANIFEST);
    }
    CHECK(am_manifest_parse((am_bytes_t){NULL,1},&meta)==AM_BAD_MANIFEST);
    char token[65];memset(token,'a',64);token[64]=0;
    am_pointer_t p={.exists=true,.previous=true},out;
    memcpy(p.current,token,65);memcpy(p.prior,token,65);uint8_t record[192];
    size_t n=am_pointer_encode(&p,record);CHECK(n>0);
    CHECK(am_pointer_parse((am_bytes_t){record,n},&out)==AM_OK);
    for(size_t i=0;i<n;i++)CHECK(am_pointer_parse((am_bytes_t){record,i},&out)==AM_BAD_RECORD);
    const char *bad[]={"[]","null","{}","{\"current\":null,\"previous\":null}",
        "{\"current\":true,\"previous\":null}","{\"current\":\"a\",\"previous\":null}"};
    for(unsigned i=0;i<6;i++)CHECK(am_pointer_parse((am_bytes_t){(const uint8_t *)bad[i],strlen(bad[i])},&out)==AM_BAD_RECORD);
    snprintf(mutated,sizeof mutated,"{\"current\":\"%s\",\"previous\":null,\"previous\":null}",token);
    CHECK(am_pointer_parse((am_bytes_t){(uint8_t *)mutated,strlen(mutated)},&out)==AM_BAD_RECORD);
    CHECK(!am_token_valid(NULL));CHECK(!am_token_valid(""));
}
static void geometry(void)
{
    am_layout_policy_t p={.qualified=true,.address=0x520000,.size=0x100000,
        .flash_size=0x800000,.reserve_bytes=4096,.table_sector_sha256={1}};
    am_partition_view_t parts[]={
        {.address=0x20000,.size=0x500000,.type=0,.label="ota"},
        {.address=0x520000,.size=0x100000,.type=1,.subtype=0x83,.label="appstore"}};
    CHECK(am_geometry_check(&p,parts,2,0x800000,p.table_sector_sha256)==AM_OK);
    const uint64_t address[]={0,0x1f000,0x51f000,0x520001,0x620000,0x630000,0x650000,
        0x7ff000,0x800000,UINT64_MAX-4095};
    for(unsigned i=0;i<10;i++) {
        am_layout_policy_t q=p;q.address=address[i];
        CHECK(am_geometry_check(&q,parts,2,0x800000,q.table_sector_sha256)==AM_DISABLED);
    }
    am_layout_policy_t q=p;q.qualified=false;
    CHECK(am_geometry_check(&q,parts,2,0x800000,q.table_sector_sha256)==AM_DISABLED);
    q=p;q.size=0;CHECK(am_geometry_check(&q,parts,2,0x800000,q.table_sector_sha256)==AM_DISABLED);
    q=p;q.size=UINT64_MAX;CHECK(am_geometry_check(&q,parts,2,0x800000,q.table_sector_sha256)==AM_DISABLED);
    q=p;q.reserve_bytes=0;CHECK(am_geometry_check(&q,parts,2,0x800000,q.table_sector_sha256)==AM_DISABLED);
    uint8_t wrong[32]={2};CHECK(am_geometry_check(&p,parts,2,0x800000,wrong)==AM_DISABLED);
    CHECK(am_geometry_check(&p,parts,1,0x800000,p.table_sector_sha256)==AM_DISABLED);
    parts[1].readonly=true;CHECK(am_geometry_check(&p,parts,2,0x800000,p.table_sector_sha256)==AM_DISABLED);
    parts[1].readonly=false;parts[0].size+=4096;
    CHECK(am_geometry_check(&p,parts,2,0x800000,p.table_sector_sha256)==AM_DISABLED);
}
int main(void) { json();geometry();printf("%u parser/geometry boundary assertions passed\n",checks);return 0; }
