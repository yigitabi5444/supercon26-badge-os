// Actual store/blob/sync logic with an atomic-blob and NVS-entry-budget model. No hardware claims.
#include "message_store.h"
#include "message_sync.h"
#include "message_nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t data[MSG_SNAPSHOT_BYTES]; size_t size, base_available; int have, fail, writes, read_error; } fixture_t;
static fixture_t f;
static message_db_t db, reopened;
static message_blob_backend_t backend, restored;
static int load(void *ctx,uint8_t *out,size_t *n)
{ fixture_t *x=ctx;if(x->read_error)return -1;if(!x->have)return 0;if(x->size>*n)return -2;memcpy(out,x->data,x->size);*n=x->size;return 1; }
static int save(void *ctx,const uint8_t *data,size_t n)
{ fixture_t *x=ctx;assert(n<=MSG_SNAPSHOT_BYTES);x->writes++;if(x->fail==1)return MSG_IO;if(x->fail==3)return MSG_FULL;memcpy(x->data,data,n);x->size=n;x->have=1;if(x->fail==4)x->read_error=1;return x->fail==2?MSG_IO:MSG_OK; }
static size_t available(void *ctx)
{ fixture_t *x=ctx;size_t used=x->have?3+(x->size+31)/32:0;return x->base_available>used?x->base_available-used:0; }
static bool empty(void *ctx) { return !((fixture_t*)ctx)->have; }
static message_blob_io_t io(void) { return(message_blob_io_t){&f,load,save,available,empty}; }
static void blank(void) { memset(&f,0,sizeof f);f.base_available=630-192; } // six 126-entry pages, GC excluded; 192 legacy entries
static message_t sample(int id,int logical)
{ message_t m={.logical=logical,.clock=1,.seconds=1794000000};m.id[0]=id;m.thread[0]=1;m.sender[0]=2;strcpy(m.title,"Meet up");strcpy(m.body,"Hello <script> & \"friends\"\nLine two.");strcpy(m.name,"Guest");return m; }
static void store_tests(void)
{
 blank();assert(message_blob_open(&db,&backend,io())==MSG_OK&&f.writes==0&&db.state.relay);
 for(int i=1;i<MSG_CAP;i++){message_t m=sample(i,i);assert(message_put(&db,&m,false)==MSG_OK);}
 assert(db.state.count==MSG_CAP-1);message_t m=sample(MSG_CAP,MSG_CAP);assert(message_put(&db,&m,false)==MSG_OK);
 int writes=f.writes;assert(db.state.count==8);assert(message_put(&db,&m,false)==MSG_DUP&&f.writes==writes);
 m.body[0]='X';assert(message_put(&db,&m,false)==MSG_CONFLICT);m=sample(99,99);
 for(int i=0;i<100;i++)assert(message_put(&db,&m,false)==MSG_FULL);
 assert(f.writes==writes&&db.state.count==8);
 assert(message_mark_read(&db,m.thread)==MSG_OK);writes=f.writes;
 assert(message_mark_read(&db,m.thread)==MSG_DUP&&f.writes==writes);assert(message_relay(&db,true)==MSG_DUP&&f.writes==writes);
 assert(message_relay(&db,false)==MSG_OK);
 assert(message_blob_open(&reopened,&restored,io())==MSG_OK&&!reopened.state.relay&&reopened.state.count==8&&reopened.state.read[0]);
 assert(!memcmp(&db.state,&reopened.state,sizeof db.state));
 assert(message_relay(&db,true)==MSG_OK);
 assert(message_blob_open(&reopened,&restored,io())==MSG_OK&&reopened.state.count==8&&reopened.state.relay&&reopened.state.read[0]);
 assert(!memcmp(&db.state,&reopened.state,sizeof db.state));
 uint8_t wire[MSG_RECORD_BYTES];message_encode(&m,wire);message_t decoded;assert(message_decode(&decoded,wire,sizeof wire));wire[341]='x';assert(!message_decode(&decoded,wire,sizeof wire));
 m.clock=0;m.seconds=1;assert(!message_valid(&m));m.seconds=0;assert(message_valid(&m));
 assert(message_forget(&db,m.thread)==MSG_OK&&db.state.count==0&&db.state.ignore_count==8);
 assert(message_next_logical(&db.state)==9);writes=f.writes;
 for(int i=1;i<=8;i++){m=sample(i,i);assert(message_put(&db,&m,false)==MSG_DELETED);}assert(writes==f.writes);
 assert(message_blob_open(&reopened,&restored,io())==MSG_OK&&reopened.state.ignore_count==8&&message_next_logical(&reopened.state)==9);
 m=sample(20,20);assert(message_put(&reopened,&m,false)==MSG_OK&&reopened.state.count==1); // new reply still allowed
 // A full snapshot plus 16 local suppression IDs is only 3,041 bytes (< one 4 KiB page before NVS overhead).
 assert(MSG_SNAPSHOT_BYTES==3041&&MSG_BLOB_ENTRY_BUDGET==99);
 puts("PASS global 7/8/9 cap, duplicates/full no-writes, read-once, metadata, reboot, deletion suppression/new reply; 6 KiB legacy entry fixture");
}
static void recovery_tests(void)
{
 // A fresh store defaults ON without a boot-time write; an explicit empty-store OFF survives reboot.
 blank();assert(message_blob_open(&db,&backend,io())==MSG_OK&&db.state.relay&&f.writes==0);
 assert(message_relay(&db,false)==MSG_OK&&db.state.count==0);
 int off_writes=f.writes;assert(message_blob_open(&reopened,&restored,io())==MSG_OK&&!reopened.state.relay&&reopened.state.count==0&&f.writes==off_writes);
 puts("PASS fresh relay ON without writes; persisted OFF retained for empty and populated stores");
 for(int existing=0;existing<2;existing++)for(int fail=1;fail<=4;fail++){
  blank();assert(message_blob_open(&db,&backend,io())==MSG_OK);message_t a=sample(1,1),b=sample(2,2);
  if(existing)assert(message_put(&db,&a,0)==MSG_OK);
  uint64_t generation=db.generation;f.fail=fail;int r=message_put(&db,&b,0);
  assert(r==(fail==3?MSG_FULL:MSG_IO)&&db.state.count==existing&&db.generation==generation);
  if(fail!=3){assert(!db.ready);int writes=f.writes;assert(message_put(&db,&b,0)==MSG_IO&&f.writes==writes);}
  f.fail=f.read_error=0;assert(message_blob_open(&reopened,&restored,io())==MSG_OK);
  assert(reopened.state.count==existing+((fail==2||fail==4)?1:0));
 }
 blank();f.read_error=1;assert(message_blob_open(&db,&backend,io())==MSG_IO&&!db.ready&&f.writes==0);
 blank();f.base_available=MSG_SETTINGS_RESERVE_ENTRIES+2*MSG_BLOB_ENTRY_BUDGET-1;
 assert(message_blob_open(&db,&backend,io())==MSG_FULL&&!db.ready&&f.writes==0);
 blank();assert(message_blob_open(&db,&backend,io())==MSG_OK);message_t m=sample(1,1);assert(message_put(&db,&m,0)==MSG_OK);
 f.data[0]^=1;int writes=f.writes;assert(message_blob_open(&reopened,&restored,io())==MSG_FORMAT&&!reopened.ready&&f.writes==writes);
 for(int operation=0;operation<3;operation++)for(int fail=1;fail<=4;fail++){
  blank();assert(message_blob_open(&db,&backend,io())==MSG_OK);message_t a=sample(1,1);assert(message_put(&db,&a,0)==MSG_OK);f.fail=fail;
  int r=operation==0?message_forget(&db,a.thread):operation==1?message_mark_read(&db,a.thread):message_relay(&db,false);
  assert(r==(fail==3?MSG_FULL:MSG_IO));f.fail=f.read_error=0;assert(message_blob_open(&reopened,&restored,io())==MSG_OK);
  bool after=fail==2||fail==4;
  if(operation==0)assert(reopened.state.count==(after?0:1)&&reopened.state.ignore_count==(after?1:0));
  if(operation==1)assert(reopened.state.read[0]==after);
  if(operation==2)assert(reopened.state.relay==!after);
 }
 // Keep exactly the last 16 deletions; live messages are never evicted. Exercise the maximum snapshot budget.
 blank();assert(message_blob_open(&db,&backend,io())==MSG_OK);
 for(int round=0;round<3;round++){
  for(int i=1;i<=8;i++){message_t a=sample(round*8+i,round*8+i);assert(message_put(&db,&a,0)==MSG_OK);}
  uint8_t thread[16]={1};assert(message_forget(&db,thread)==MSG_OK);
 }
 assert(db.state.ignore_count==16);uint8_t old[16]={1},recent[16]={24};assert(!message_ignored(&db.state,old)&&message_ignored(&db.state,recent));
 for(int i=25;i<=32;i++){message_t a=sample(i,i);assert(message_put(&db,&a,0)==MSG_OK);}assert(f.size==3041);
 puts("PASS failed delete/read/relay transactions, 16-ID bounded retention, maximum 3,041-byte snapshot");
 puts("PASS first/replacement atomic failure before/after commit/readback, full/unavailable storage, unknown format preserved");
}

static uint8_t frames[80][MSG_PACKET_MAX];static size_t lengths[80];static int used;
static bool send_ok=true;
static bool capture(const uint8_t peer[6],const uint8_t *data,size_t n)
{(void)peer;assert(n<=MSG_PACKET_MAX);assert(used<80);memcpy(frames[used],data,n);lengths[used++]=n;return send_ok;}
static void dispatch_tests(void)
{
 uint8_t p[MSG_PACKET_MAX+1]={0},peer[6]={1};message_state_t state={.relay=1};message_t out;
 const char *magic[]={"BOSR","PONG","BMSG"};
 for(int m=0;m<3;m++)for(size_t n=0;n<=sizeof p;n++){
  memcpy(p,magic[m],4);p[4]=2;message_sync_t s={0},zero={0};
  assert(message_frame(p,n)==(m==2&&n>=4));assert(!message_sync_receive(&s,&state,peer,p,n,0,&out));assert(!memcmp(&s,&zero,sizeof s));
 }
 memcpy(p,"BMSG",4);p[4]=1;
 for(int t=1;t<=3;t++)for(size_t n=0;n<=sizeof p;n++){p[5]=t;message_sync_t s={0},zero={0};assert(!message_sync_receive(&s,&state,peer,p,n,0,&out));assert(!memcmp(&s,&zero,sizeof s));}
 puts("PASS BOSR/PONG dispatch, malformed lengths/zero IDs/unknown version remain inert");
}
static void sync_tests(void)
{
    message_state_t a={.count=1,.relay=1},b={.relay=1};a.messages[0]=sample(1,1);message_sync_t sa={0},sb={0};
    uint8_t pa[6]={1},pb[6]={2};message_t complete;used=0;
    message_sync_tick(&sa,&a,0,capture);assert(used==1);
    assert(!message_sync_receive(&sb,&b,pa,frames[0],lengths[0],0,&complete));
    message_sync_tick(&sb,&b,0,capture);assert(used==2);
    assert(!message_sync_receive(&sa,&a,pb,frames[1],lengths[1],0,&complete));
    message_sync_tick(&sa,&a,250,capture);message_sync_tick(&sa,&a,500,capture);assert(used==4);
    assert(!message_sync_receive(&sb,&b,pa,frames[3],lengths[3],500,&complete)); // second fragment first
    assert(!message_sync_receive(&sb,&b,pa,frames[3],lengths[3],500,&complete)); // duplicate
    assert(!message_sync_receive(&sb,&b,pa,frames[2],lengths[2]-1,500,&complete)); // truncated
    assert(!message_sync_receive(&sb,&b,pb,frames[2],lengths[2],500,&complete)); // wrong peer
    uint8_t damaged[MSG_PACKET_MAX];memcpy(damaged,frames[2],lengths[2]);damaged[23]^=1;
    assert(!message_sync_receive(&sb,&b,pa,damaged,lengths[2],500,&complete)); // fragment checksum mismatch
    assert(message_sync_receive(&sb,&b,pa,frames[2],lengths[2],500,&complete));
    uint8_t expected[MSG_RECORD_BYTES],actual[MSG_RECORD_BYTES];message_encode(&a.messages[0],expected);message_encode(&complete,actual);assert(!memcmp(expected,actual,sizeof expected));
    b.messages[b.count++]=complete;assert(!message_sync_receive(&sb,&b,pa,frames[0],lengths[0],600,&complete));assert(!sb.receiving);
    // Off means no copying/sending; missing fragments use exactly three WANTs and cooldown.
    b.relay=0;message_sync_tick(&sb,&b,700,capture);assert(!sb.receiving);
    memset(&sb,0,sizeof sb);b.count=0;b.relay=1;used=1; // retain original offer
    message_sync_receive(&sb,&b,pa,frames[0],lengths[0],1000,&complete);
    for(int t=1000;t<=9000;t+=250)message_sync_tick(&sb,&b,t,capture);
    assert(used==4&&!sb.receiving&&sb.cooldown>9000);
    message_sync_receive(&sb,&b,pa,frames[0],lengths[0],9000,&complete);assert(!sb.receiving);
    // Delete/free-space then encounter: old copies provoke no WANT, payload reassembly or writes.
    message_state_t retired={.relay=1,.ignore_count=1};memcpy(retired.ignored[0],a.messages[0].id,16);
    memset(&sb,0,sizeof sb);used=1;
    for(int t=10000;t<30000;t+=250){assert(!message_sync_receive(&sb,&retired,pa,frames[0],lengths[0],t,&complete));message_sync_tick(&sb,&retired,t,capture);}
    assert(used==1&&!sb.receiving);
    sb.receiving=true;memcpy(sb.wanted,a.messages[0].id,16);message_sync_tick(&sb,&retired,30000,capture);assert(!sb.receiving&&used==1);
    // Failed driver sends do not establish delivery; attempts stay bounded and a later restart succeeds.
    memset(&sb,0,sizeof sb);send_ok=false;used=1;b.count=0;b.relay=1;
    message_sync_receive(&sb,&b,pa,frames[0],lengths[0],31000,&complete);
    for(int t=31000;t<=39000;t+=250)message_sync_tick(&sb,&b,t,capture);
    assert(used==4&&!sb.receiving&&b.count==0);send_ok=true;memset(&sb,0,sizeof sb);used=1;
    message_sync_receive(&sb,&b,pa,frames[0],lengths[0],50000,&complete);message_sync_tick(&sb,&b,50000,capture);
    message_sync_receive(&sa,&a,pb,frames[1],lengths[1],50000,&complete);message_sync_tick(&sa,&a,50000,capture);message_sync_tick(&sa,&a,50250,capture);
    assert(!message_sync_receive(&sb,&b,pa,frames[2],lengths[2],50250,&complete));assert(message_sync_receive(&sb,&b,pa,frames[3],lengths[3],50250,&complete));
    puts("PASS two-badge loss/reorder/duplicate/CRC, delete then encounters (no refill/resend), bounded failed sends/restart");
}
int main(void){store_tests();recovery_tests();dispatch_tests();sync_tests();return 0;}
