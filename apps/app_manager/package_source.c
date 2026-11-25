#include <string.h>
#include "package_source.h"
#include "managed_apps.h"

static bool host_valid(const char *host,size_t n)
{
    if(!n || n>253 || host[0]=='.' || host[n-1]=='.')return false;
    for(size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)host[i];
        if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='-' || c=='.'))return false;
        if((c=='.' && (i==0 || host[i-1]=='.' || host[i-1]=='-')) ||
           (c=='-' && (i==0 || host[i-1]=='.' || i+1==n)))return false;
    }
    return true;
}
static bool local_valid(const char *authority)
{
    if(!authority || strnlen(authority,22)>20)return false;
    const char *p=authority;
    for(unsigned i=0;i<4;i++) {
        const char *start=p;unsigned value=0,digits=0;
        while(*p>='0' && *p<='9') { if(++digits>3)return false;value=value*10+(unsigned)(*p++-'0'); }
        if(!digits || value>255 || (digits>1 && *start=='0'))return false;
        if(i<3 && *p++!='.')return false;
    }
    return !strcmp(p,":8443");
}
static bool url_valid(const char *url,const char *authority,bool local)
{
    if(!url || strnlen(url,AM_URL_MAX+1)>AM_URL_MAX || strncmp(url,"https://",8))return false;
    const char *end=strchr(url+8,'/');if(!end)return false;
    size_t n=(size_t)(end-url-8);
    if(local) { if(!local_valid(authority) || strlen(authority)!=n || memcmp(url+8,authority,n))return false; }
    else if(!host_valid(url+8,n) || (authority && (strlen(authority)!=n || memcmp(url+8,authority,n))))return false;
    for(const unsigned char *p=(const unsigned char *)end;*p;p++)
        if(*p<=32 || *p>126 || *p=='@' || *p=='#' || *p=='\\')return false;
    return true;
}
bool am_source_url_valid(const char *url,const char *host)
{ return url_valid(url,host,false); }
bool am_source_url_allowed(const am_source_config_t *c,const char *url)
{ return c && (c->local_authority!=NULL)==(c->tls_ca_pem!=NULL) &&
    url_valid(url,c->local_authority,c->local_authority!=NULL); }
bool am_source_config_valid(const am_source_config_t *c)
{
    if(!c || (c->local_authority!=NULL)!=(c->tls_ca_pem!=NULL))return false;
    if(c->local_authority && (!local_valid(c->local_authority) || !c->tls_ca_pem[0] ||
       strnlen(c->tls_ca_pem,AM_TLS_CA_MAX+1)>AM_TLS_CA_MAX))return false;
    return c->public_key && c->key_size==65 && c->public_key[0]==4 &&
        am_source_url_allowed(c,c->manifest_url) && am_source_url_allowed(c,c->signature_url) &&
        am_source_url_allowed(c,c->payload_url) &&
        (!c->redirect_host || host_valid(c->redirect_host,strlen(c->redirect_host)));
}
am_result_t am_source_cached(const am_source_t *s,am_package_t *package)
{
    if(!package)return AM_IO;
    memset(package,0,sizeof *package);
    if(!s || !s->valid)return AM_DISABLED;
    *package=s->package;return AM_OK;
}
am_result_t am_source_fetch(am_source_t *s,const am_crypto_t *crypto,const am_runtime_t *runtime,
                            am_source_get_t get,void *context,bool (*cancel)(void *),void *owner)
{
    if(!s)return AM_IO;
    s->valid=false;memset(&s->package,0,sizeof s->package);
    const char *id=s->app_id?s->app_id:"aliens";
    if(!am_managed_id(id))return AM_DISABLED;
    if(!am_source_config_valid(s->config))return AM_DISABLED;
    if(!s->bytes || !get || !crypto || !crypto->verify_p256 || !runtime || !cancel)return AM_IO;
    const char *urls[]={s->config->manifest_url,s->config->signature_url,s->config->payload_url};
    bool doom=!strcmp(id,"doom");if(doom && !s->doom_payload)return AM_IO;
    uint8_t *buffers[]={s->bytes->manifest,s->bytes->signature,doom?s->doom_payload:s->bytes->payload};
    size_t caps[]={AM_MANIFEST_MAX,AM_SIGNATURE_MAX,doom?AM_DOOM_PAYLOAD_MAX:AM_PAYLOAD_SIZE};size_t sizes[3]={0};
    for(unsigned i=0;i<3;i++) {
        if(cancel(owner))return AM_CANCELLED;
        am_result_t r=get(context,urls[i],buffers[i],caps[i],&sizes[i]);
        if(r!=AM_OK)return r;
        if(cancel(owner))return AM_CANCELLED;
        if(!sizes[i] || sizes[i]>caps[i])return AM_LIMIT;
        if(i==1) { // Authenticate/parse the release BEFORE requesting its payload.
            uint8_t raw[64];am_metadata_t meta;
            r=am_signature_raw((am_bytes_t){buffers[1],sizes[1]},raw);
            if(r==AM_OK)r=crypto->verify_p256(crypto->context,buffers[0],sizes[0],raw);
            if(r==AM_OK)r=am_manifest_parse((am_bytes_t){buffers[0],sizes[0]},&meta);
            if(r!=AM_OK)return r;
            if(strcmp(meta.id,id) || !am_package_compatible(&meta,runtime))return AM_DISABLED;
            if(doom)caps[2]=meta.size;
        }
    }
    am_package_t p={{buffers[0],sizes[0]},{buffers[1],sizes[1]},{buffers[2],sizes[2]}};
    if(s->progress)s->progress("Verifying...");
    if(cancel(owner))return AM_CANCELLED;
    am_metadata_t meta;char token[65];am_result_t r=am_package_integrity(crypto,p,&meta,token);
    if(r!=AM_OK)return r;
    if(cancel(owner))return AM_CANCELLED;
    s->package=p;s->valid=true;return AM_OK;
}
