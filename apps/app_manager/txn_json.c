// Deliberately bounded flat ASCII JSON schema, not a general JSON framework.
#include <stdio.h>
#include <string.h>
#include "txn.h"

typedef struct { const uint8_t *p, *end; } cursor_t;
static void space(cursor_t *c)
{ while(c->p<c->end && (*c->p==' ' || *c->p=='\t' || *c->p=='\r' || *c->p=='\n')) c->p++; }
static bool take(cursor_t *c, uint8_t ch)
{ space(c);if(c->p==c->end || *c->p!=ch)return false;c->p++;return true; }
static bool string(cursor_t *c, char *out, size_t cap)
{
    if(!take(c,'"'))return false;
    size_t n=0;
    while(c->p<c->end && *c->p!='"') {
        uint8_t ch=*c->p++;
        if(ch<32 || ch>126 || ch=='\\' || n+1>=cap)return false;
        out[n++]=(char)ch;
    }
    if(c->p==c->end)return false;
    c->p++;out[n]=0;return true;
}
static bool number(cursor_t *c, uint32_t *out)
{
    space(c);if(c->p==c->end || *c->p<'0' || *c->p>'9')return false;
    bool zero=*c->p=='0';uint32_t value=0;unsigned n=0;
    while(c->p<c->end && *c->p>='0' && *c->p<='9') {
        if((zero && n) || value>(UINT32_MAX-(unsigned)(*c->p-'0'))/10)return false;
        value=value*10+(*c->p++-'0');n++;
    }
    *out=value;return true;
}
static bool end(cursor_t *c) { space(c);return c->p==c->end; }
bool am_token_valid(const char *s)
{
    if(!s)return false;
    for(unsigned i=0;i<64;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;
    return s[64]==0;
}
static bool version(am_metadata_t *m)
{
    const char *p=m->version;
    for(unsigned i=0;i<3;i++) {
        uint32_t n=0;unsigned digits=0;
        while(*p>='0' && *p<='9') { if(++digits>6)return false;n=n*10+(*p++-'0'); }
        if(!digits || (i<2?*p++!='.':*p!=0))return false;
        m->parts[i]=n;
    }
    return true;
}
am_result_t am_manifest_parse(am_bytes_t input, am_metadata_t *m)
{
    if(!m || !input.data || !input.size || input.size>AM_MANIFEST_MAX)return AM_BAD_MANIFEST;
    cursor_t c={input.data,input.data+input.size};unsigned seen=0;uint32_t schema=0;
    memset(m,0,sizeof *m);
    if(!take(&c,'{'))return AM_BAD_MANIFEST;
    for(unsigned count=0;count<9;count++) {
        char key[16];unsigned bit=0;bool ok=false;
        if(!string(&c,key,sizeof key) || !take(&c,':'))return AM_BAD_MANIFEST;
        if(!strcmp(key,"schema")){bit=1;ok=number(&c,&schema);}
        else if(!strcmp(key,"id")){bit=2;ok=string(&c,m->id,sizeof m->id);}
        else if(!strcmp(key,"version")){bit=4;ok=string(&c,m->version,sizeof m->version);}
        else if(!strcmp(key,"target")){bit=8;ok=string(&c,m->target,sizeof m->target);}
        else if(!strcmp(key,"abi")){bit=16;ok=number(&c,&m->abi);}
        else if(!strcmp(key,"sdk")){bit=32;ok=string(&c,m->sdk,sizeof m->sdk);}
        else if(!strcmp(key,"loader")){bit=64;ok=string(&c,m->loader,sizeof m->loader);}
        else if(!strcmp(key,"bytes")){bit=128;ok=number(&c,&m->size);}
        else if(!strcmp(key,"sha256")){bit=256;ok=string(&c,m->hash,sizeof m->hash);}
        if(!ok || !bit || (seen&bit))return AM_BAD_MANIFEST;
        seen|=bit;
        if(count<8 && !take(&c,','))return AM_BAD_MANIFEST;
    }
    if(!take(&c,'}') || !end(&c) || seen!=511 || schema!=1 || !version(m) ||
       !m->abi || !m->target[0] || !m->sdk[0] || !m->loader[0] ||
       m->size<52 || m->size>(!strcmp(m->id,"doom")?AM_DOOM_PAYLOAD_MAX:AM_PAYLOAD_MAX) || !am_token_valid(m->hash))return AM_BAD_MANIFEST;
    if(!m->id[0])return AM_BAD_MANIFEST;
    for(const char *p=m->id;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_'))return AM_BAD_MANIFEST;
    return AM_OK;
}
am_result_t am_pointer_parse(am_bytes_t input, am_pointer_t *out)
{
    if(!out || !input.data || !input.size || input.size>512)return AM_BAD_RECORD;
    cursor_t c={input.data,input.data+input.size};unsigned seen=0;
    memset(out,0,sizeof *out);
    if(!take(&c,'{'))return AM_BAD_RECORD;
    for(unsigned i=0;i<2;i++) {
        char key[16];unsigned bit;
        if(!string(&c,key,sizeof key) || !take(&c,':'))return AM_BAD_RECORD;
        if(!strcmp(key,"current"))bit=1;
        else if(!strcmp(key,"previous"))bit=2;
        else return AM_BAD_RECORD;
        if(seen&bit)return AM_BAD_RECORD;
        seen|=bit;space(&c);
        if(bit==2 && c.end-c.p>=4 && !memcmp(c.p,"null",4))c.p+=4;
        else {
            char *value=bit==1?out->current:out->prior;
            if(!string(&c,value,65) || !am_token_valid(value))return AM_BAD_RECORD;
            if(bit==2)out->previous=true;
        }
        if(!i && !take(&c,','))return AM_BAD_RECORD;
    }
    if(!take(&c,'}') || !end(&c) || seen!=3)return AM_BAD_RECORD;
    out->exists=true;return AM_OK;
}
size_t am_pointer_encode(const am_pointer_t *p, uint8_t out[192])
{
    if(!p || !out || !am_token_valid(p->current) || (p->previous && !am_token_valid(p->prior)))return 0;
    int n=p->previous?snprintf((char *)out,192,"{\"current\":\"%s\",\"previous\":\"%s\"}",p->current,p->prior):
        snprintf((char *)out,192,"{\"current\":\"%s\",\"previous\":null}",p->current);
    return n>0 && n<192?(size_t)n:0;
}
