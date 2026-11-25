// Focused admission tests; no loader execution, signing, device, or firmware build.
#include "../native_elf.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { NUL,HASH,DYNSYM,DYNSTR,RELADYN,RELAPLT,TEXT,RODATA,GOTLOC,GOT,DATA,DRLRO,BSS,NOTE,SHSTR,COUNT };
enum { CAP=AM_NATIVE_PAYLOAD_MAX+1, SHOFF=4096, LENGTH=SHOFF+COUNT*40 };
static uint8_t image[CAP],good[CAP];
static uint32_t offsets[COUNT],sizes[COUNT],cursor;
static uint32_t get(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8)); }
static void half(uint8_t *p,unsigned n) { p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8); }
static uint8_t *sh(unsigned i) { return image+SHOFF+i*40; }
static void field(unsigned i,unsigned word,uint32_t n) { put(sh(i)+word*4,n); }
static void sym(unsigned i,unsigned n,uint32_t value,uint32_t size,unsigned type,unsigned sec)
{
    uint8_t *p=image+offsets[DYNSYM]+i*16;
    put(p,n);put(p+4,value);put(p+8,size);p[12]=(uint8_t)type;half(p+14,sec);
}
static void rela(unsigned sec,unsigned i,uint32_t where,unsigned symbol,unsigned type,uint32_t addend)
{
    uint8_t *p=image+offsets[sec]+i*12;
    put(p,where);put(p+4,symbol*256+type);put(p+8,addend);
}
static void add(unsigned i,unsigned type,unsigned flags,uint32_t addr,unsigned size,unsigned align,unsigned ent)
{
    cursor=(cursor+align-1)&~(align-1);offsets[i]=cursor;sizes[i]=size;
    field(i,1,type);field(i,2,flags);field(i,3,addr);field(i,4,cursor);field(i,5,size);field(i,8,align);field(i,9,ent);
    if(type!=8)cursor+=size;
}
static void fixture(void)
{
    static const char names[]="\0.hash\0.dynsym\0.dynstr\0.rela.dyn\0.rela.plt\0.text\0.rodata\0.got.loc\0.got\0.data\0.data.rel.ro\0.bss\0.badge_app\0.shstrtab\0";
    static const char dynstr[]="\0badge_app_get_api\0grid\0app_main\0_end\0";
    memset(image,0,sizeof image);memset(offsets,0,sizeof offsets);cursor=128;
    memcpy(image,"\177ELF\1\1\1",7);half(image+16,3);half(image+18,94);put(image+20,1);
    put(image+24,0x118);put(image+28,52);put(image+32,SHOFF);put(image+36,0x300);
    half(image+40,52);half(image+42,32);half(image+44,2);half(image+46,40);half(image+48,COUNT);half(image+50,SHSTR);
    add(HASH,5,2,0x2000,32,4,4);add(DYNSYM,11,2,0x2100,80,4,16);
    add(DYNSTR,3,2,0x2200,sizeof dynstr,1,0);add(RELADYN,4,2,0x2300,60,4,12);add(RELAPLT,4,66,0x2400,12,4,12);
    add(TEXT,1,6,0x100,32,4,0);add(RODATA,1,2,0x120,8,4,0);add(GOTLOC,1,2,0x128,24,4,0);
    add(GOT,1,3,0x300,4,4,0);add(DATA,1,3,0x200,8,4,0);add(DRLRO,1,3,0x208,4,4,0);
    add(BSS,8,3,0x20c,8,4,0);add(NOTE,1,0,0,48,4,0);add(SHSTR,3,0,0,sizeof names,1,0);
    memcpy(image+offsets[DYNSTR],dynstr,sizeof dynstr);memcpy(image+offsets[SHSTR],names,sizeof names);
    unsigned nameoff=1;for(unsigned i=1;i<COUNT;i++){field(i,0,nameoff);nameoff+=(unsigned)strlen(names+nameoff)+1;}
    field(HASH,6,DYNSYM);field(DYNSYM,6,DYNSTR);field(DYNSYM,7,1);
    field(RELADYN,6,DYNSYM);field(RELAPLT,6,DYNSYM);field(RELAPLT,7,GOT);
    put(image+offsets[HASH],1);put(image+offsets[HASH]+4,5);put(image+offsets[HASH]+8,1);
    sym(1,1,0,0,16,0);sym(2,19,0x120,8,17,RODATA);sym(3,24,0x118,8,18,TEXT);sym(4,33,0x304,0,16,SHSTR);
    rela(RELADYN,0,0x100,0,2,1);rela(RELADYN,1,0x104,0,2,2);
    rela(RELADYN,2,0x10c,0,5,0);rela(RELADYN,3,0x200,2,3,4);rela(RELADYN,4,0x204,0,5,0);
    rela(RELAPLT,0,0x108,1,4,0);put(image+offsets[TEXT]+12,0x120);put(image+offsets[DATA]+4,0x208);
    put(image+offsets[GOTLOC],0x100);put(image+offsets[GOTLOC]+4,24);put(image+offsets[GOT],0x300);
    uint8_t *note=image+offsets[NOTE];put(note,0x32504142);put(note+4,2);memcpy(note+8,"aliens",6);memcpy(note+24,"1.0.0",5);
    uint8_t *ph=image+52;put(ph,1);put(ph+4,offsets[TEXT]);put(ph+8,0x100);put(ph+12,0x100);put(ph+16,32);put(ph+20,32);put(ph+24,5);put(ph+28,4);
    ph+=32;put(ph,1);put(ph+4,offsets[DATA]);put(ph+8,0x200);put(ph+12,0x200);put(ph+16,12);put(ph+20,20);put(ph+24,6);put(ph+28,4);
    memcpy(good,image,sizeof good);
}
static am_result_t check(size_t size) { return am_native_elf_check((am_bytes_t){image,size},"aliens","1.0.0",2); }
static void expect(am_result_t wanted,int line)
{
    am_result_t actual=check(LENGTH);
    if(actual!=wanted){fprintf(stderr,"line %d: expected %d, got %d\n",line,wanted,actual);exit(1);}
    memcpy(image,good,sizeof good);
}
#define EXPECT(n) expect(n,__LINE__)
static void headers(void)
{
    EXPECT(AM_OK);
    // A new version and changed executable bytes need no firmware-side hash list.
    memcpy(image+offsets[NOTE]+24,"1.0.1",5);put(image+offsets[TEXT]+24,0xa5a5a5a5);
    assert(am_native_elf_check((am_bytes_t){image,LENGTH},"aliens","1.0.1",2)==AM_OK);
    memcpy(image,good,sizeof good);
    image[0]=0;EXPECT(AM_BAD_MANIFEST);image[4]=2;EXPECT(AM_BAD_MANIFEST);image[5]=2;EXPECT(AM_BAD_MANIFEST);
    image[6]=2;EXPECT(AM_BAD_MANIFEST);image[7]=3;EXPECT(AM_BAD_MANIFEST);image[15]=1;EXPECT(AM_BAD_MANIFEST);
    half(image+16,2);EXPECT(AM_BAD_MANIFEST);half(image+18,243);EXPECT(AM_BAD_MANIFEST);
    put(image+20,2);EXPECT(AM_BAD_MANIFEST);put(image+36,0x100);EXPECT(AM_BAD_MANIFEST);
    half(image+40,51);EXPECT(AM_BAD_MANIFEST);half(image+46,41);EXPECT(AM_BAD_MANIFEST);
    put(image+32,SHOFF+1);EXPECT(AM_BAD_MANIFEST);put(image+32,UINT32_MAX);EXPECT(AM_BAD_MANIFEST);
    half(image+48,65);assert(check(AM_NATIVE_PAYLOAD_MAX)==AM_LIMIT);EXPECT(AM_BAD_MANIFEST);half(image+50,COUNT);EXPECT(AM_BAD_MANIFEST);
    put(image+24,0);EXPECT(AM_BAD_MANIFEST);put(image+24,0x119);EXPECT(AM_BAD_MANIFEST);put(image+24,0x120);EXPECT(AM_BAD_MANIFEST);
    half(image+44,17);EXPECT(AM_LIMIT);half(image+42,31);EXPECT(AM_BAD_MANIFEST);
    put(image+28,UINT32_MAX);EXPECT(AM_BAD_MANIFEST);put(image+52,7);EXPECT(AM_BAD_MANIFEST);
    put(image+52+16,33);EXPECT(AM_BAD_MANIFEST);put(image+52+8,UINT32_MAX-8);EXPECT(AM_BAD_MANIFEST);
    put(image+52+24,7);EXPECT(AM_BAD_MANIFEST);put(image+52+28,3);EXPECT(AM_BAD_MANIFEST);
    assert(check(AM_NATIVE_PAYLOAD_MAX)==AM_OK);assert(check(CAP)==AM_LIMIT);
    assert(am_native_elf_check((am_bytes_t){NULL,LENGTH},"aliens","1.0.0",2)==AM_BAD_MANIFEST);
    assert(am_native_elf_check((am_bytes_t){image,LENGTH},"counter","1.0.0",2)==AM_BAD_MANIFEST);
    assert(am_native_elf_check((am_bytes_t){image,LENGTH},"aliens","1.0.0",1)==AM_BAD_MANIFEST);
    assert(am_native_elf_check((am_bytes_t){image,LENGTH},"aliens","01.0.0",2)==AM_BAD_MANIFEST);
    for(size_t n=0;n<LENGTH;n++)assert(check(n)==AM_BAD_MANIFEST);
    uint8_t *unaligned=malloc(LENGTH+1);assert(unaligned);memcpy(unaligned+1,image,LENGTH);
    assert(am_native_elf_check((am_bytes_t){unaligned+1,LENGTH},"aliens","1.0.0",2)==AM_OK);free(unaligned);
}
static void sections(void)
{
    field(0,3,1);EXPECT(AM_BAD_MANIFEST);field(TEXT,0,sizes[SHSTR]);EXPECT(AM_BAD_MANIFEST);
    image[offsets[SHSTR]+sizes[SHSTR]-1]=1;EXPECT(AM_BAD_MANIFEST);
    field(DATA,4,UINT32_MAX);EXPECT(AM_BAD_MANIFEST);field(DATA,5,UINT32_MAX);EXPECT(AM_BAD_MANIFEST);
    field(BSS,3,UINT32_MAX-4);EXPECT(AM_BAD_MANIFEST);field(DATA,8,3);EXPECT(AM_BAD_MANIFEST);
    field(DATA,8,8192);EXPECT(AM_BAD_MANIFEST);field(DATA,4,offsets[DATA]+1);EXPECT(AM_BAD_MANIFEST);
    field(DATA,3,0x121);EXPECT(AM_BAD_MANIFEST);field(DATA,3,0x120);EXPECT(AM_BAD_MANIFEST);
    // A 29-byte text file occupies 32 mapped bytes. Adjacent file sections still overlap the map.
    field(TEXT,5,29);put(image+offsets[DYNSYM]+3*16+8,4);EXPECT(AM_OK);
    field(TEXT,5,29);field(RODATA,3,0x11d);field(RODATA,8,1);EXPECT(AM_BAD_MANIFEST);
    // Relocations may not write into the executable allocation's rounded padding.
    field(TEXT,5,29);put(image+offsets[DYNSYM]+3*16+8,4);rela(RELADYN,2,0x11c,0,5,0);EXPECT(AM_BAD_MANIFEST);
    field(TEXT,2,7);EXPECT(AM_BAD_MANIFEST);field(DATA,1,8);EXPECT(AM_BAD_MANIFEST);
    field(GOTLOC,3,0x124);EXPECT(AM_BAD_MANIFEST);field(GOTLOC,5,23);EXPECT(AM_BAD_MANIFEST);
    field(GOTLOC,3,0x40000);EXPECT(AM_LIMIT);field(GOT,5,8);EXPECT(AM_BAD_MANIFEST);
    field(GOT,0,get(sh(TEXT)));EXPECT(AM_BAD_MANIFEST);
    memcpy(image+offsets[SHSTR]+get(sh(GOT)),".tls",5);EXPECT(AM_BAD_MANIFEST);
    field(BSS,5,AM_NATIVE_MEMORY_MAX-64-11-11-7-3);EXPECT(AM_OK);
    field(BSS,5,AM_NATIVE_MEMORY_MAX-64-11-11-7-3+1);EXPECT(AM_LIMIT);
    field(NOTE,1,7);EXPECT(AM_BAD_MANIFEST);field(NOTE,2,2);EXPECT(AM_BAD_MANIFEST);
    field(NOTE,5,47);EXPECT(AM_BAD_MANIFEST);field(NOTE,8,1);EXPECT(AM_BAD_MANIFEST);
    image[offsets[NOTE]]=0;EXPECT(AM_BAD_MANIFEST);put(image+offsets[NOTE]+4,1);EXPECT(AM_BAD_MANIFEST);
    image[offsets[NOTE]+8]='c';EXPECT(AM_BAD_MANIFEST);image[offsets[NOTE]+15]=1;EXPECT(AM_BAD_MANIFEST);
    image[offsets[NOTE]+24]='2';EXPECT(AM_BAD_MANIFEST);image[offsets[NOTE]+47]=1;EXPECT(AM_BAD_MANIFEST);
    field(NOTE,0,get(sh(DATA)));EXPECT(AM_BAD_MANIFEST);
}
static void file_symbol_fixture(void)
{
    sym(1,33,0,0,4,0xfff1);sym(2,1,0,0,16,0);sym(3,19,0x120,8,17,RODATA);sym(4,24,0x118,8,18,TEXT);
    field(DYNSYM,7,2);rela(RELADYN,3,0x200,3,3,4);rela(RELAPLT,0,0x108,2,4,0);
}
static void tables(void)
{
    field(DYNSYM,9,15);EXPECT(AM_BAD_MANIFEST);field(DYNSYM,5,79);EXPECT(AM_BAD_MANIFEST);
    field(DYNSYM,6,COUNT);EXPECT(AM_BAD_MANIFEST);field(DYNSYM,6,TEXT);EXPECT(AM_BAD_MANIFEST);
    field(DYNSYM,7,6);EXPECT(AM_BAD_MANIFEST);image[offsets[DYNSTR]+sizes[DYNSTR]-1]=1;EXPECT(AM_BAD_MANIFEST);
    image[offsets[DYNSYM]]=1;EXPECT(AM_BAD_MANIFEST);put(image+offsets[DYNSYM]+16,sizes[DYNSTR]);EXPECT(AM_BAD_MANIFEST);
    memcpy(image+offsets[DYNSTR]+1,"malloc",7);EXPECT(AM_BAD_MANIFEST);image[offsets[DYNSYM]+16+12]=32;EXPECT(AM_BAD_MANIFEST);
    put(image+offsets[DYNSYM]+16+4,1);EXPECT(AM_BAD_MANIFEST);image[offsets[DYNSYM]+16+13]=1;EXPECT(AM_BAD_MANIFEST);
    half(image+offsets[DYNSYM]+2*16+14,COUNT);EXPECT(AM_BAD_MANIFEST);
    half(image+offsets[DYNSYM]+2*16+14,GOT);EXPECT(AM_BAD_MANIFEST);
    put(image+offsets[DYNSYM]+2*16+4,0x128);EXPECT(AM_BAD_MANIFEST);
    put(image+offsets[DYNSYM]+2*16+8,9);EXPECT(AM_BAD_MANIFEST);
    half(image+offsets[DYNSYM]+3*16+14,RODATA);put(image+offsets[DYNSYM]+3*16+4,0x120);EXPECT(AM_BAD_MANIFEST);
    put(image+offsets[DYNSYM]+4*16+4,UINT32_MAX);EXPECT(AM_BAD_MANIFEST);
    field(DYNSYM,5,1025*16);field(HASH,4,8192);field(HASH,5,8+(1+1025)*4);
    put(image+8192,1);put(image+8196,1025);assert(check(AM_NATIVE_PAYLOAD_MAX)==AM_LIMIT);memcpy(image,good,sizeof good);
    put(image+offsets[HASH],0);EXPECT(AM_BAD_MANIFEST);put(image+offsets[HASH]+4,4);EXPECT(AM_BAD_MANIFEST);
    put(image+offsets[HASH]+8,5);EXPECT(AM_BAD_MANIFEST);field(HASH,6,TEXT);EXPECT(AM_BAD_MANIFEST);
    field(RELADYN,9,8);EXPECT(AM_BAD_MANIFEST);field(RELADYN,5,59);EXPECT(AM_BAD_MANIFEST);
    field(RELADYN,6,COUNT);EXPECT(AM_BAD_MANIFEST);field(RELADYN,6,DYNSTR);EXPECT(AM_BAD_MANIFEST);
    field(RELADYN,7,COUNT);EXPECT(AM_BAD_MANIFEST);field(RELADYN,1,9);EXPECT(AM_BAD_MANIFEST);
    rela(RELAPLT,0,0x108,1,1,0);EXPECT(AM_BAD_MANIFEST);rela(RELAPLT,0,0x108,1,255,0);EXPECT(AM_BAD_MANIFEST);
    rela(RELAPLT,0,0x108,5,4,0);EXPECT(AM_BAD_MANIFEST);rela(RELAPLT,0,0x109,1,4,0);EXPECT(AM_BAD_MANIFEST);
    rela(RELAPLT,0,0x300,1,4,0);EXPECT(AM_BAD_MANIFEST);rela(RELAPLT,0,0x210,1,4,0);EXPECT(AM_BAD_MANIFEST);
    rela(RELAPLT,0,0x11c,1,4,0);EXPECT(AM_OK);rela(RELAPLT,0,0x120-3,1,4,0);EXPECT(AM_BAD_MANIFEST);
    rela(RELAPLT,0,0x108,1,4,1);EXPECT(AM_BAD_MANIFEST);rela(RELAPLT,0,0x108,0,4,0);EXPECT(AM_BAD_MANIFEST);
    rela(RELAPLT,0,0x108,4,4,0);EXPECT(AM_BAD_MANIFEST);
    // A legal unreferenced STT_FILE/SHN_ABS record must never be dereferenced as a section.
    file_symbol_fixture();EXPECT(AM_OK);
    file_symbol_fixture();rela(RELAPLT,0,0x108,1,4,0);EXPECT(AM_BAD_MANIFEST);
    rela(RELADYN,3,0x200,2,3,7);EXPECT(AM_OK);rela(RELADYN,3,0x200,2,3,8);EXPECT(AM_BAD_MANIFEST);
    rela(RELADYN,3,0x200,2,3,UINT32_MAX);EXPECT(AM_BAD_MANIFEST);
    rela(RELADYN,2,0x10c,1,5,0);EXPECT(AM_BAD_MANIFEST);rela(RELADYN,2,0x10c,0,5,1);EXPECT(AM_BAD_MANIFEST);
    put(image+offsets[TEXT]+12,0x300);EXPECT(AM_BAD_MANIFEST);put(image+offsets[TEXT]+12,UINT32_MAX);EXPECT(AM_BAD_MANIFEST);
    rela(RELADYN,0,0x100,0,2,3);EXPECT(AM_BAD_MANIFEST);rela(RELADYN,0,0x100,1,2,1);EXPECT(AM_BAD_MANIFEST);
    rela(RELADYN,0,0x110,0,2,1);EXPECT(AM_BAD_MANIFEST);put(image+offsets[TEXT],1);EXPECT(AM_BAD_MANIFEST);
    rela(RELADYN,1,0x104,0,5,0);put(image+offsets[TEXT]+4,0x120);EXPECT(AM_BAD_MANIFEST);
    rela(RELAPLT,0,0x200,1,4,0);EXPECT(AM_BAD_MANIFEST); // duplicate across relocation tables
    field(RELADYN,5,2049*12);assert(check(AM_NATIVE_PAYLOAD_MAX)==AM_LIMIT);memcpy(image,good,sizeof good);
    field(RELAPLT,5,0);EXPECT(AM_BAD_MANIFEST); // must actually bind the sole API import
}
static void mutation_safety(void)
{
    for(unsigned i=0;i<LENGTH;i++) {
        image[i]^=0x80;am_result_t result=check(LENGTH);
        assert(result==AM_OK || result==AM_BAD_MANIFEST || result==AM_LIMIT);image[i]^=0x80;
    }
    uint32_t seed=0x261114;
    for(unsigned i=0;i<10000;i++) {
        unsigned positions[4];for(unsigned j=0;j<4;j++){seed=1664525*seed+1013904223;positions[j]=seed%LENGTH;image[positions[j]]^=(uint8_t)(seed>>24);}
        am_result_t result=check(LENGTH);assert(result==AM_OK || result==AM_BAD_MANIFEST || result==AM_LIMIT);
        memcpy(image,good,sizeof good);
    }
}
static void external(const char *path,const char *version,am_result_t wanted)
{
    FILE *f=fopen(path,"rb");assert(f);size_t size=fread(image,1,sizeof image,f);assert(!ferror(f) && feof(f));fclose(f);
    assert(size && am_native_elf_check((am_bytes_t){image,size},"aliens",version,2)==wanted);memcpy(image,good,sizeof good);
}
int main(int argc,char **argv)
{
    fixture();headers();sections();tables();mutation_safety();
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--accept")){assert(i+2<argc);external(argv[i+2],argv[i+1],AM_OK);i+=2;}
        else external(argv[i],"1.0.0",AM_BAD_MANIFEST);
    }
    puts("PASS: native ELF profile, rounded mapping/budget boundaries, metadata/import/relocation tables, truncation and mutation safety");
}
