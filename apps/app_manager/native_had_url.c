#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "native_had_url.h"

bool am_had_url(const char *url,bool feed,char host[254],const char **path,bool *tls)
{
    if(!url || !url[0] || strnlen(url,400)>399)return false;
    *tls=!strncmp(url,"https://",8);
    if(!*tls && (feed || strncmp(url,"http://",7)))return false;
    const char *authority=url+(*tls?8:7),*end=strchr(authority,'/');
    if(!end)return false;
    size_t n=(size_t)(end-authority);
    if(!n || n>253 || authority[0]=='.' || authority[n-1]=='.')return false;
    for(size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)authority[i];if(c>='A' && c<='Z')c=(unsigned char)(c+'a'-'A');
        if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='-' || c=='.'))return false;
        if((c=='.' && (i==0 || authority[i-1]=='.' || authority[i-1]=='-')) ||
           (c=='-' && (i==0 || authority[i-1]=='.' || i+1==n)))return false;
        host[i]=(char)c;
    }
    host[n]=0;
    for(const unsigned char *p=(const unsigned char *)end;*p;p++)
        if(*p<=32 || *p>126 || *p=='@' || *p=='#' || *p=='\\')return false;
    *path=end;return true;
}
bool am_had_public_ipv4(uint32_t ip)
{
    uint32_t first=ip>>24;
    if(!first || first==10 || first==127 || first>=224)return false;
    if((ip&0xffc00000u)==0x64400000u || (ip&0xffff0000u)==0xa9fe0000u ||
       (ip&0xfff00000u)==0xac100000u || (ip&0xffffff00u)==0xc0000000u ||
       (ip&0xffffff00u)==0xc0000200u || (ip&0xffffff00u)==0xc0586300u ||
       (ip&0xffff0000u)==0xc0a80000u || (ip&0xfffe0000u)==0xc6120000u ||
       (ip&0xffffff00u)==0xc6336400u || (ip&0xffffff00u)==0xcb007100u)return false;
    return true;
}
bool am_had_redirect(const char *base,const char *location,bool feed,char out[400])
{
    char host[254];const char *path;bool tls;
    if(!location || !location[0] || strnlen(location,400)>399 || !am_had_url(base,feed,host,&path,&tls))return false;
    int n;
    if(!strncmp(location,"http://",7) || !strncmp(location,"https://",8))n=snprintf(out,400,"%s",location);
    else if(location[0]=='/' && location[1]=='/')n=snprintf(out,400,"%s:%s",tls?"https":"http",location);
    else if(location[0]=='/')n=snprintf(out,400,"%s://%s%s",tls?"https":"http",host,location);
    else {
        const char *end=strchr(path,'?');if(!end)end=path+strlen(path);
        if(location[0]!='?')while(end>path && end[-1]!='/')end--;
        n=snprintf(out,400,"%s://%s%.*s%s",tls?"https":"http",host,(int)(end-path),path,location);
    }
    return n>0 && n<400 && am_had_url(out,feed,host,&path,&tls);
}
