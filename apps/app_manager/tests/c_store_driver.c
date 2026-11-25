// Disposable host-store CLI for cross-language fixtures and process-failure tests.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../host_c/adapters.h"

static am_result_t input(const char *folder,const char *name,uint8_t *out,size_t cap,size_t *size)
{
    char path[AM_VFS_PATH_MAX];int n=snprintf(path,sizeof path,"%s/%s",folder,name);
    if(n<0 || (size_t)n>=sizeof path)return AM_LIMIT;
    FILE *file=fopen(path,"rb");if(!file)return AM_IO;
    *size=fread(out,1,cap,file);int extra=fgetc(file),bad=ferror(file);int closed=fclose(file);
    return extra!=EOF?AM_LIMIT:bad || closed?AM_IO:AM_OK;
}
int main(int argc,char **argv)
{
    if(argc!=10){fprintf(stderr,"root pubkey operation package runtimeABI quota checkpoint mode writefull\n");return 2;}
    am_store_t store={0};am_host_t host;am_metadata_t meta={0};
    am_workspace_t *work=calloc(1,sizeof *work),*incoming=calloc(1,sizeof *incoming);
    if(!work || !incoming)return 2;
    store.work=work;store.app_id="aliens";store.runtime=(am_runtime_t){"esp32s3","6.0.2","2.1.0",(uint32_t)strtoul(argv[5],NULL,10)};
    am_result_t r=am_host_io(&host,argv[1],strtoull(argv[6],NULL,10),&store.io);
    if(r==AM_OK)r=am_openssl_crypto(argv[2],&store.crypto);
    if(r!=AM_OK){free(work);free(incoming);printf("{\"result\":%d}\n",r);return 0;}
    host.fault=atoi(argv[7]);host.mode=atoi(argv[8]);host.write_full=atoi(argv[9])!=0;
    if(!strcmp(argv[3],"install") || !strcmp(argv[3],"check")) {
        size_t lengths[3]={0};r=input(argv[4],"manifest",incoming->manifest,AM_MANIFEST_MAX,&lengths[0]);
        if(r==AM_OK)r=input(argv[4],"signature",incoming->signature,AM_SIGNATURE_MAX,&lengths[1]);
        if(r==AM_OK)r=input(argv[4],"module.elf",incoming->payload,AM_PAYLOAD_SIZE,&lengths[2]);
        am_package_t p={{incoming->manifest,lengths[0]},{incoming->signature,lengths[1]},{incoming->payload,lengths[2]}};
        char token[65];
        if(r==AM_OK)r=!strcmp(argv[3],"install")?am_store_install(&store,p):
            am_package_integrity(&store.crypto,p,&meta,token);
    } else if(!strcmp(argv[3],"recover"))r=am_store_recover(&store);
    else if(!strcmp(argv[3],"uninstall"))r=am_store_uninstall(&store);
    else if(!strcmp(argv[3],"lookup"))r=am_store_lookup(&store,&meta);
    else if(!strcmp(argv[3],"lease")) {
        _Alignas(4) uint8_t copy[AM_PAYLOAD_SIZE];am_lease_t lease={0},foreign={0};
        r=am_store_snapshot(&store,&meta,copy,sizeof copy,&lease);
        if(r==AM_OK) {
            uint8_t digest[32];char encoded[65];static const char hex[]="0123456789abcdef";
            if(store.crypto.sha256(store.crypto.context,copy,meta.size,digest)!=AM_OK)return 2;
            for(unsigned i=0;i<32;i++){encoded[i*2]=hex[digest[i]>>4];encoded[i*2+1]=hex[digest[i]&15];}
            encoded[64]=0;
            foreign=lease;
            if(strcmp(encoded,meta.hash) || am_store_uninstall(&store)!=AM_BUSY ||
               am_store_recover(&store)!=AM_BUSY || am_store_release(&store,&foreign)!=AM_BAD_RECORD)
                r=AM_IO;
            am_result_t released=am_store_release(&store,&lease);
            if(released!=AM_OK || am_store_release(&store,&lease)!=AM_BAD_RECORD)r=AM_IO;
        }
    }
    else r=AM_IO;
    printf("{\"result\":%d,\"version\":\"%s\",\"installed\":%s,\"workspace\":%zu}\n",
        r,meta.version,(r==AM_OK || r==AM_DISABLED) && !strcmp(argv[3],"lookup")?"true":"false",sizeof *work);
    am_openssl_close(&store.crypto);free(work);free(incoming);return 0;
}
