#include "native_elf.h"
#include "managed_apps.h"
#include <string.h>

// Read bytes explicitly: preflight must also be safe for unaligned package data.
enum { EH=52, SH=40, PH=32, SYM=16, RELA=12, SECTIONS_MAX=64,
       SYMBOLS_MAX=1024, RELOCS_MAX=2048, ALLOC=2, WRITE=1, EXEC=4 };
typedef struct { uint32_t name,type,flags,addr,off,size,link,info,align,ent; } section_t;
typedef struct { uint32_t name,value,size; uint8_t info,other; uint16_t section; } symbol_t;
typedef struct {
    am_bytes_t bytes; uint32_t shoff; uint16_t count;
    section_t strings, mapped[6]; unsigned seen;
    bool doom;
} image_t;
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0]|(uint16_t)p[1]<<8); }
static bool range(size_t limit,uint32_t off,uint32_t size)
{ return off<=limit && size<=limit-off; }
static bool contains(const section_t *s,uint32_t addr,uint32_t size)
{ return addr>=s->addr && (uint64_t)addr+size<=(uint64_t)s->addr+s->size; }
static bool power(uint32_t n) { return n && !(n&(n-1)); }
static section_t section(const image_t *im,unsigned i)
{
    const uint8_t *p=im->bytes.data+im->shoff+i*SH;
    section_t s={u32(p),u32(p+4),u32(p+8),u32(p+12),u32(p+16),
                 u32(p+20),u32(p+24),u32(p+28),u32(p+32),u32(p+36)};
    return s;
}
static symbol_t symbol(const image_t *im,const section_t *s,unsigned i)
{
    const uint8_t *p=im->bytes.data+s->off+i*SYM;
    symbol_t v={u32(p),u32(p+4),u32(p+8),p[12],p[13],u16(p+14)};return v;
}
static const char *string(const image_t *im,const section_t *s,uint32_t off)
{ return off<s->size?(const char *)im->bytes.data+s->off+off:NULL; }
static const char *name(const image_t *im,const section_t *s)
{ return string(im,&im->strings,s->name); }
static int slot(const char *n)
{
    static const char *const names[]={".text",".bss",".data",".rodata",".data.rel.ro",".got.loc"};
    for(unsigned i=0;i<6;i++)if(!strcmp(n,names[i]))return (int)i;
    return -1;
}
static int mapped(const image_t *im,uint32_t addr,uint32_t size)
{
    for(unsigned i=0;i<6;i++)if((im->seen&(1u<<i)) && contains(&im->mapped[i],addr,size))return (int)i;
    return -1;
}
static bool padded(const uint8_t *p,size_t n,const char *value)
{
    size_t len=strlen(value);
    if(len>=n || memcmp(p,value,len))return false;
    for(size_t i=len;i<n;i++)if(p[i])return false;
    return true;
}
static bool version_valid(const char *p)
{
    if(!p)return false;
    for(unsigned i=0;i<3;i++) {
        unsigned digits=0;const char *start=p;
        while(*p>='0' && *p<='9'){p++;if(++digits>6)return false;}
        if(!digits || (digits>1 && *start=='0') || (i<2?*p++!='.':*p!=0))return false;
    }
    return true;
}
static bool string_table(const image_t *im,const section_t *s)
{
    return s->type==3 && s->size && range(im->bytes.size,s->off,s->size) &&
           !im->bytes.data[s->off] && !im->bytes.data[s->off+s->size-1];
}
static bool runtime_symbol(const image_t *im,const symbol_t *v)
{
    if(!v->section || v->section>=im->count)return false;
    section_t s=section(im,v->section);int k=slot(name(im,&s));
    return k>=0 && (im->seen&(1u<<k)) && contains(&s,v->value,v->size?v->size:1);
}
static am_result_t layout(image_t *im,uint32_t entry)
{
    uint64_t allocation=0;unsigned text=im->seen&1;
    if(!entry || !text || !im->mapped[0].size || !contains(&im->mapped[0],entry,1) || (entry&3))return AM_BAD_MANIFEST;
    // map_sym() sees rounded text, not the original file size. Check this first.
    for(unsigned i=0;i<6;i++)if(im->seen&(1u<<i)) {
        section_t s=im->mapped[i];uint64_t size=i? s.size:((uint64_t)s.size+3)&~UINT64_C(3);
        if((uint64_t)s.addr+size>UINT32_MAX)return AM_BAD_MANIFEST;
        for(unsigned j=i+1;j<6;j++)if(im->seen&(1u<<j)) {
            section_t t=im->mapped[j];
            if(size && t.size && (uint64_t)s.addr+size>t.addr && (uint64_t)t.addr+t.size>s.addr)return AM_BAD_MANIFEST;
        }
        if(i!=0 && i!=5 && s.size)allocation+=(uint64_t)s.size+s.align-1;
    }
    const section_t *t=&im->mapped[0],*g=&im->mapped[5];
    uint64_t span=((uint64_t)t->size+3)&~UINT64_C(3);
    if((im->seen&(1u<<5)) && g->size) {
        if(g->addr<(uint64_t)t->addr+t->size || ((g->addr-t->addr)&3))return AM_BAD_MANIFEST;
        span=((uint64_t)g->addr+g->size-t->addr+3)&~UINT64_C(3);
    }
    return allocation+span>(im->doom?AM_DOOM_MEMORY_MAX:AM_NATIVE_MEMORY_MAX)?AM_LIMIT:AM_OK;
}
static bool table_shape(const section_t *s,unsigned width)
{ return !(s->off&3) && s->align==4 && s->ent==width && !(s->size%width); }
static bool end_marker(const image_t *im,uint32_t value)
{
    for(unsigned i=1;i<im->count;i++) {
        section_t s=section(im,i);const char *n=name(im,&s);
        if((slot(n)>=0 || !strcmp(n,".got")) &&
           ((uint64_t)s.addr+s.size==value || (!strcmp(n,".bss") && s.addr==value)))return true;
    }
    return false;
}
static am_result_t symbols(const image_t *im,const section_t *s)
{
    unsigned count=s->size/SYM;
    if(!table_shape(s,SYM) || !count || s->link>=im->count || s->info>count)return AM_BAD_MANIFEST;
    if(count>SYMBOLS_MAX)return AM_LIMIT;
    section_t strings=section(im,s->link);
    if(!string_table(im,&strings))return AM_BAD_MANIFEST;
    for(unsigned i=0;i<count;i++) {
        symbol_t v=symbol(im,s,i);const char *n=string(im,&strings,v.name);
        unsigned bind=v.info>>4,type=v.info&15;
        if(!n || bind>2 || type>4 || v.other || (i<s->info?bind!=0:bind==0))return AM_BAD_MANIFEST;
        if(!i) {
            const uint8_t *p=im->bytes.data+s->off;
            for(unsigned j=0;j<SYM;j++)if(p[j])return AM_BAD_MANIFEST;
        } else if(!v.section) {
            if(strcmp(n,"badge_app_get_api") || bind!=1 || (type!=0 && type!=2) || v.value || v.size)return AM_BAD_MANIFEST;
        } else if(type==4 && bind==0 && v.section==0xfff1) {
            if(v.value || v.size)return AM_BAD_MANIFEST; // STT_FILE, never a relocation source.
        } else if(!runtime_symbol(im,&v)) {
            // GNU ld emits unreferenced end markers even after stripping .dynamic.
            if(type || bind!=1 || v.size || v.section>=im->count || !end_marker(im,v.value) ||
               (strcmp(n,"_end") && strcmp(n,"_edata") && strcmp(n,"__bss_start")))return AM_BAD_MANIFEST;
        } else if(!strcmp(n,"badge_app_get_api")) {
            return AM_BAD_MANIFEST;
        }
        if(type==2 && v.section) {
            section_t target=section(im,v.section);
            if(strcmp(name(im,&target),".text") || (v.value&3))return AM_BAD_MANIFEST;
        }
    }
    return AM_OK;
}
static bool repeated(const image_t *im,unsigned sec,unsigned index,uint32_t off)
{
    for(unsigned i=0;i<=sec;i++) {
        section_t s=section(im,i);if(s.type!=4)continue;
        unsigned end=i==sec?index:s.size/RELA;
        for(unsigned j=0;j<end;j++)if(u32(im->bytes.data+s.off+j*RELA)==off)return true;
    }
    return false;
}
static am_result_t relocations(const image_t *im)
{
    unsigned count=0,imports=0,markers=0;
    for(unsigned i=0;i<im->count;i++) {
        section_t s=section(im,i);if(s.type!=4)continue;
        if(!table_shape(&s,RELA) || s.link>=im->count || s.info>=im->count)return AM_BAD_MANIFEST;
        section_t symtab=section(im,s.link);
        if(symtab.type!=2 && symtab.type!=11)return AM_BAD_MANIFEST;
        count+=s.size/RELA;if(count>(im->doom?AM_DOOM_RELOCS_MAX:RELOCS_MAX))return AM_LIMIT;
        for(unsigned j=0;j<s.size/RELA;j++) {
            const uint8_t *p=im->bytes.data+s.off+j*RELA;
            uint32_t off=u32(p),info=u32(p+4),addend=u32(p+8),si=info>>8,type=info&255;
            int k=mapped(im,off,4);
            if((off&3) || k<0 || k==1 || si>=symtab.size/SYM || repeated(im,i,j,off))return AM_BAD_MANIFEST;
            symbol_t v=symbol(im,&symtab,si);
            if(type==2) { // Pinned Xtensa RTLD markers are ignored by arch_relocate.
                if(si || k!=0 || (addend!=1 && addend!=2) ||
                   off!=im->mapped[0].addr+(addend-1)*4 || (markers&(1u<<addend)) ||
                   u32(im->bytes.data+im->mapped[k].off+off-im->mapped[k].addr))return AM_BAD_MANIFEST;
                markers|=1u<<addend;
            } else if(type==5) { // RELATIVE maps the stored word; this loader ignores A.
                uint32_t target=u32(im->bytes.data+im->mapped[k].off+off-im->mapped[k].addr);
                if(si || addend || mapped(im,target,1)<0)return AM_BAD_MANIFEST;
            } else if(type==3 || type==4) {
                if(!si)return AM_BAD_MANIFEST;
                if(!v.section) { if(addend)return AM_BAD_MANIFEST;imports++; }
                else {
                    int64_t target=(int64_t)v.value+(int32_t)addend;
                    if(!runtime_symbol(im,&v) || target<0 || target>UINT32_MAX)return AM_BAD_MANIFEST;
                    section_t owner=section(im,v.section);
                    if(!contains(&owner,(uint32_t)target,1))return AM_BAD_MANIFEST;
                }
            } else return AM_BAD_MANIFEST;
        }
    }
    return imports && (!markers || markers==6)?AM_OK:AM_BAD_MANIFEST;
}
static bool hash_table(const image_t *im,const section_t *s)
{
    if(!table_shape(s,4) || s->size<8 || s->link>=im->count)return false;
    section_t syms=section(im,s->link);const uint8_t *p=im->bytes.data+s->off;
    uint32_t buckets=u32(p),chains=u32(p+4);
    if(syms.type!=11 || !buckets || chains!=syms.size/SYM || (uint64_t)buckets+chains!=(s->size-8)/4)return false;
    for(unsigned i=2;i<s->size/4;i++)if(u32(p+i*4)>=chains)return false;
    return true;
}
am_result_t am_native_elf_check(am_bytes_t bytes,const char *app_id,const char *version,uint32_t abi)
{
    bool doom=app_id && !strcmp(app_id,"doom");
    if(bytes.size>(doom?AM_DOOM_PAYLOAD_MAX:AM_NATIVE_PAYLOAD_MAX))return AM_LIMIT;
    if(!bytes.data || bytes.size<EH || !am_managed_id(app_id) || abi!=2 || !version_valid(version))return AM_BAD_MANIFEST;
    const uint8_t *p=bytes.data;image_t im={.bytes=bytes,.shoff=u32(p+32),.count=u16(p+48),.doom=doom};
    static const uint8_t ident[16]={127,'E','L','F',1,1,1};
    uint32_t phoff=u32(p+28),entry=u32(p+24);unsigned phnum=u16(p+44),strindex=u16(p+50);
    if(memcmp(p,ident,16) || u16(p+16)!=3 || u16(p+18)!=94 || u32(p+20)!=1 ||
       u32(p+36)!=0x300 || u16(p+40)!=EH || u16(p+46)!=SH || !im.count || !strindex || strindex>=im.count ||
       (im.shoff&3) || im.shoff<EH || !range(bytes.size,im.shoff,(uint32_t)im.count*SH))return AM_BAD_MANIFEST;
    if(im.count>SECTIONS_MAX || phnum>16)return AM_LIMIT;
    if(phnum? (u16(p+42)!=PH || (phoff&3) || phoff<EH || !range(bytes.size,phoff,phnum*PH)) : (phoff || u16(p+42)))return AM_BAD_MANIFEST;
    for(unsigned i=0;i<phnum;i++) {
        const uint8_t *q=p+phoff+i*PH;uint32_t type=u32(q),off=u32(q+4),addr=u32(q+8),file=u32(q+16),mem=u32(q+20),flags=u32(q+24),align=u32(q+28);
        if((type!=1 && type!=2) || (flags!=5 && flags!=6) || file>mem || !range(bytes.size,off,file) ||
           (uint64_t)addr+mem>UINT32_MAX || !power(align) || (type==1 && ((addr-off)&(align-1))) || (type==2 && (file || mem)))return AM_BAD_MANIFEST;
    }
    im.strings=section(&im,strindex);
    if(!string_table(&im,&im.strings))return AM_BAD_MANIFEST;
    // Complete structural pass before any linked table or symbol is followed.
    for(unsigned i=0;i<im.count;i++) {
        section_t s=section(&im,i);
        if(s.name>=im.strings.size || (s.type!=8 && !range(bytes.size,s.off,s.size)) ||
           (s.type==8 && s.off>bytes.size) || (uint64_t)s.addr+s.size>UINT32_MAX ||
           (s.align && (!power(s.align) || s.align>4096)) ||
           (s.align>1 && ((s.off&(s.align-1)) || ((s.flags&ALLOC) && (s.addr&(s.align-1))))))return AM_BAD_MANIFEST;
        if(!i) { for(unsigned j=0;j<SH;j++)if(p[im.shoff+j])return AM_BAD_MANIFEST;continue; }
        int k=slot(name(&im,&s));
        if(k>=0) { if(im.seen&(1u<<k))return AM_BAD_MANIFEST;im.seen|=1u<<k;im.mapped[k]=s; }
    }
    am_result_t result=layout(&im,entry);if(result!=AM_OK)return result;
    unsigned metadata=0,dynsym=0,symbol_count=0;
    for(unsigned i=1;i<im.count;i++) {
        section_t s=section(&im,i);const char *n=name(&im,&s);int k=slot(n);
        if((!strcmp(n,".hash") && s.type!=5) || (!strcmp(n,".dynsym") && s.type!=11) ||
           ((!strcmp(n,".dynstr") || !strcmp(n,".strtab") || !strcmp(n,".shstrtab")) && s.type!=3) ||
           (!strcmp(n,".symtab") && s.type!=2))return AM_BAD_MANIFEST;
        if(k>=0) {
            unsigned flags=k==0?ALLOC|EXEC:(k==1 || k==2 || k==4)?ALLOC|WRITE:ALLOC;
            if(s.type!=(k==1?8u:1u) || s.flags!=flags || s.align<4 || s.ent || s.link || s.info || (s.addr&3) || (s.off&3) || (k==5 && (s.size&3)))return AM_BAD_MANIFEST;
        } else if(!strcmp(n,".badge_app")) {
            if(metadata++ || s.type!=1 || s.flags || s.size!=48 || s.align!=4 || s.ent || s.link || s.info ||
               u32(p+s.off)!=0x32504142 || u32(p+s.off+4)!=abi || !padded(p+s.off+8,16,app_id) || !padded(p+s.off+24,24,version))return AM_BAD_MANIFEST;
        } else if(s.type==2 || s.type==11) {
            if(s.flags!=(s.type==11?ALLOC:0u) || (s.type==11 && (strcmp(n,".dynsym") || dynsym++)))return AM_BAD_MANIFEST;
            symbol_count+=s.size/SYM;if(symbol_count>SYMBOLS_MAX)return AM_LIMIT;
            result=symbols(&im,&s);if(result!=AM_OK)return result;
        } else if(s.type==3) {
            if(!string_table(&im,&s) || s.ent || s.link || s.info || s.flags!=(!strcmp(n,".dynstr")?ALLOC:0u))return AM_BAD_MANIFEST;
        } else if(s.type==4) {
            if((s.flags!=ALLOC && s.flags!=(ALLOC|64u)) || strncmp(n,".rela.",6))return AM_BAD_MANIFEST;
        } else if(s.type==5) {
            if(strcmp(n,".hash") || s.flags!=ALLOC || s.info || !hash_table(&im,&s))return AM_BAD_MANIFEST;
        } else if(!strcmp(n,".got")) {
            // GNU ld retains this bookkeeping word; the section loader never maps it.
            if(s.type!=1 || s.flags!=(ALLOC|WRITE) || s.align!=4 || s.size!=4 || s.ent || s.link || s.info)return AM_BAD_MANIFEST;
        } else if(s.type!=1 || s.flags || s.link || s.info || s.ent) return AM_BAD_MANIFEST;
    }
    if(metadata!=1 || dynsym!=1)return AM_BAD_MANIFEST;
    return relocations(&im);
}
