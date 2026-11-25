// Actual source/HTTP/parser/integrity code; crypto, native ELF, TLS and radio are doubles.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../package_http.h"
#include "../native_elf.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_netif_sntp.h"

typedef struct {
    int status;size_t declared,actual;const uint8_t *body;const char *location,*encoding;
    bool chunked,duplicate;
} response_t;
struct local_http_double { esp_http_client_config_t config;size_t got;unsigned index; };
static response_t responses[8];
static unsigned clients,opens,cleans,sets,reads,joins,offs,ranges,verifies,hashes,natives;
static bool cancelled,cancel_read,cancel_wifi,wifi_ok,verify_ok,source_phase;
static int64_t now_us;
static const uint8_t key[65]={4};
static const char ca[]="-----BEGIN CERTIFICATE-----\ncontrolled-test-double\n-----END CERTIFICATE-----\n";
static const uint8_t signature[]={0x30,6,2,1,1,2,1,1};
static uint8_t payload[AM_PAYLOAD_MAX];
static char manifest[AM_MANIFEST_MAX];
static size_t payload_size;
static time_t wall_clock=1700001000;
static unsigned syncs,waits,stops;
static bool connected,sync_ok=true,cancel_sync;
static const char *phases[4];static unsigned phase_count;
static am_result_t native_result;
static am_source_bytes_t buffers;
static const am_source_config_t public_config={
    .manifest_url="https://github.com/o/r/manifest",.signature_url="https://github.com/o/r/signature",
    .payload_url="https://github.com/o/r/payload",.redirect_host="release-assets.githubusercontent.com",
    .public_key=key,.key_size=sizeof key,
};
static const am_source_config_t local_config={
    .manifest_url="https://192.168.1.8:8443/manifest",.signature_url="https://192.168.1.8:8443/signature",
    .payload_url="https://192.168.1.8:8443/payload",.public_key=key,.key_size=sizeof key,
    .local_authority="192.168.1.8:8443",.tls_ca_pem=ca,
};
static const am_source_config_t *current;
static void reset(const am_source_config_t *config)
{
    assert(!clients);memset(responses,0,sizeof responses);
    responses[0]=(response_t){.status=200,.declared=3,.actual=3,.body=(const uint8_t *)"abc"};
    clients=opens=cleans=sets=reads=joins=offs=ranges=verifies=hashes=natives=0;
    cancelled=cancel_read=cancel_wifi=source_phase=false;wifi_ok=verify_ok=true;native_result=AM_OK;now_us=10;current=config;
    connected=false;syncs=waits=stops=phase_count=0;sync_ok=true;cancel_sync=false;
}
time_t am_test_time(time_t *out){if(out)*out=wall_clock;return wall_clock;}
static void progress(const char *phase){assert(phase_count<4);phases[phase_count++]=phase;}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c)
{assert(connected && !strcmp(c->server,"pool.ntp.org"));syncs++;return ESP_OK;}
esp_err_t esp_netif_sntp_sync_wait(TickType_t ticks)
{
    assert(ticks==250);waits++;now_us+=(int64_t)ticks*1000;
    if(cancel_sync)cancelled=true;
    if(!sync_ok || cancel_sync)return ESP_ERR_TIMEOUT;
    wall_clock=1700001000;return ESP_OK;
}
void esp_netif_sntp_deinit(void){assert(syncs==1);stops++;}
static bool cancel(void *context){(void)context;return cancelled;}
int64_t esp_timer_get_time(void){return now_us;}
bool wifi_connect(int ms){assert(ms==AM_WIFI_MS);joins++;if(cancel_wifi)cancelled=true;connected=wifi_ok;return wifi_ok;}
bool wifi_connected(void){return connected;}
void wifi_off(void){assert(!clients);connected=false;offs++;}
void ranging_start(void){assert(!clients && offs);ranges++;}
esp_err_t esp_crt_bundle_attach(void *ctx){(void)ctx;return ESP_OK;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c)
{
    assert(c->transport_type==HTTP_TRANSPORT_OVER_SSL && c->disable_auto_redirect);
    assert(!c->skip_cert_common_name_check && !c->common_name && !c->use_global_ca_store);
    assert(c->cert_pem==current->tls_ca_pem);
    assert(c->crt_bundle_attach==(current->local_authority?NULL:esp_crt_bundle_attach));
    assert(c->timeout_ms==AM_HTTP_IO_MS && c->buffer_size==2048 && c->buffer_size_tx==4096);
    assert(am_source_url_allowed(current,c->url));
    struct local_http_double *h=calloc(1,sizeof *h);assert(h);h->config=*c;clients++;return h;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t c,int ms)
{assert(c && ms>0 && ms<=AM_HTTP_IO_MS);return ESP_OK;}
esp_err_t esp_http_client_open(esp_http_client_handle_t c,int length)
{assert(c && !length && opens<8);c->index=opens++;c->got=0;return ESP_OK;}
static void event(esp_http_client_handle_t c,char *name,const char *value)
{
    esp_http_client_event_t e={.event_id=HTTP_EVENT_ON_HEADER,.client=c,.user_data=c->config.user_data,
        .header_key=name,.header_value=(char *)value};assert(c->config.event_handler(&e)==ESP_OK);
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c)
{
    response_t *r=&responses[c->index];
    if(r->location){event(c,"Location",r->location);if(r->duplicate)event(c,"Location",r->location);}
    if(r->encoding)event(c,"Content-Encoding",r->encoding);
    return (int64_t)r->declared;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c){return responses[c->index].status;}
bool esp_http_client_is_chunked_response(esp_http_client_handle_t c){return responses[c->index].chunked;}
int esp_http_client_read(esp_http_client_handle_t c,char *out,int length)
{
    assert(length==1);reads++;if(cancel_read)cancelled=true;response_t *r=&responses[c->index];
    if(c->got>=r->actual)return 0;
    *out=(char)r->body[c->got++];return 1;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c)
{return c->got==responses[c->index].actual && c->got==responses[c->index].declared;}
esp_err_t esp_http_client_close(esp_http_client_handle_t c){assert(c);return ESP_OK;}
esp_err_t esp_http_client_set_url(esp_http_client_handle_t c,const char *url)
{assert(c && !current->local_authority && am_source_url_valid(url,current->redirect_host));sets++;return ESP_OK;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c)
{assert(c && clients==1);clients--;cleans++;free(c);return ESP_OK;}
static am_result_t verify(void *ctx,const uint8_t *bytes,size_t size,const uint8_t raw[64])
{
    (void)ctx;assert(bytes==(const uint8_t *)buffers.manifest && size && raw[31]==1 && raw[63]==1);
    if(source_phase)assert(opens==(verifies?3u:2u));verifies++;return verify_ok?AM_OK:AM_BAD_AUTH;
}
static am_result_t sha256(void *ctx,const uint8_t *bytes,size_t size,uint8_t out[32])
{(void)ctx;assert(bytes && size);hashes++;memset(out,0,32);return AM_OK;}
am_result_t am_native_elf_check(am_bytes_t bytes,const char *id,const char *version,uint32_t abi)
{assert(bytes.data==buffers.payload && bytes.size==payload_size && !strcmp(id,"aliens") && !strcmp(version,"1.0.0") && abi==2);natives++;return native_result;}
static void configs(void)
{
    assert(!am_source_config_valid(&am_app_source_config));assert(am_source_config_valid(&public_config));assert(am_source_config_valid(&local_config));
    am_source_config_t c=local_config;c.local_authority=NULL;assert(!am_source_config_valid(&c));
    c=local_config;c.tls_ca_pem=NULL;assert(!am_source_config_valid(&c));assert(!am_source_url_allowed(&c,c.payload_url));c.tls_ca_pem="";assert(!am_source_config_valid(&c));
    static char large[AM_TLS_CA_MAX+2];memset(large,'x',AM_TLS_CA_MAX);c.tls_ca_pem=large;assert(am_source_config_valid(&c));
    large[AM_TLS_CA_MAX]='x';assert(!am_source_config_valid(&c));
    const char *bad[]={"", "192.168.1.8", "192.168.1.8:443", "192.168.1.8:08443", "192.168.1.8:8443/",
        "192.168.001.8:8443", "192.168.1.008:8443", "256.168.1.8:8443", "192.168.1:8443", "192.168.1.8.1:8443",
        "0xc0.168.1.8:8443", "3232235784:8443", "host.local:8443", "[::1]:8443", "user@192.168.1.8:8443",
        "192.168.1.8:8443#x", "192.168.1.8:8443\\x", "192.168.1.8:8443\n"};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++){c=local_config;c.local_authority=bad[i];assert(!am_source_config_valid(&c));}
    const char *urls[]={"https://192.168.1.9:8443/manifest", "https://192.168.01.8:8443/manifest", "https://192.168.1.8:8443.evil/manifest",
        "https://192.168.1.8/manifest", "https://192.168.1.8:443/manifest", "https://user@192.168.1.8:8443/manifest",
        "https://192.168.1.8:8443/manifest#x", "https://192.168.1.8:8443/manifest\\x", "https://192.168.1.8:8443/mani\rfest",
        "http://192.168.1.8:8443/manifest", "HTTPS://192.168.1.8:8443/manifest", "/manifest"};
    for(unsigned i=0;i<sizeof urls/sizeof *urls;i++) {
        c=local_config;c.manifest_url=urls[i];assert(!am_source_config_valid(&c));
        c=local_config;c.signature_url=urls[i];assert(!am_source_config_valid(&c));
        c=local_config;c.payload_url=urls[i];assert(!am_source_config_valid(&c));
        assert(!am_source_url_allowed(&local_config,urls[i]));
    }
    c=local_config;c.local_authority="255.0.1.255:8443";
    c.manifest_url=c.signature_url=c.payload_url="https://255.0.1.255:8443/file";assert(am_source_config_valid(&c));
    assert(!am_source_url_valid(local_config.payload_url,NULL)); // Public port policy is unchanged.
    assert(am_source_url_valid("https://github.com/a?x=1",NULL));
}
static am_result_t download(size_t cap,const char *url)
{
    am_https_t http;size_t size=99;static uint8_t out[AM_PAYLOAD_MAX+1];
    am_result_t r=am_https_begin(&http,current,cancel,NULL,NULL);
    if(r==AM_OK)r=am_https_get(&http,url,out,cap,&size);
    if(r==AM_OK)assert(size==responses[opens-1].declared && !memcmp(out,responses[opens-1].body,size));
    else if(opens)assert(size==0);
    am_https_end(&http);assert(!clients && offs==joins && ranges==offs);return r;
}
static void transport(void)
{
    reset(&local_config);assert(download(AM_PAYLOAD_MAX,local_config.manifest_url)==AM_OK && opens==1 && reads==3);
    reset(&local_config);assert(download(AM_PAYLOAD_MAX,public_config.manifest_url)==AM_DISABLED && !opens);
    reset(&local_config);assert(download(AM_PAYLOAD_MAX+1,local_config.payload_url)==AM_DISABLED && !opens);
    const int redirects[]={301,302,303,307,308};
    for(unsigned i=0;i<5;i++) {
        reset(&local_config);responses[0].status=redirects[i];responses[0].location=local_config.payload_url;
        assert(download(AM_PAYLOAD_MAX,local_config.manifest_url)==AM_DISABLED && opens==1 && !sets && !reads);
        reset(&public_config);responses[1]=responses[0];responses[0].status=redirects[i];responses[0].location="https://release-assets.githubusercontent.com/p?token=x";
        assert(download(AM_MANIFEST_MAX,public_config.manifest_url)==AM_OK && opens==2 && sets==1);
    }
    am_source_config_t redirect_local=local_config;redirect_local.redirect_host=public_config.redirect_host;
    reset(&redirect_local);responses[0].status=302;responses[0].location="https://release-assets.githubusercontent.com/p";
    assert(download(AM_PAYLOAD_MAX,local_config.manifest_url)==AM_DISABLED && opens==1 && !sets);
    reset(&public_config);responses[2]=responses[0];responses[0].status=302;
    responses[0].location="https://release-assets.githubusercontent.com/p";responses[1]=responses[0];
    assert(download(AM_MANIFEST_MAX,public_config.manifest_url)==AM_OK && opens==3 && sets==2);
    reset(&public_config);responses[0].status=302;responses[0].location="https://release-assets.githubusercontent.com/p";
    responses[1]=responses[2]=responses[0];assert(download(AM_MANIFEST_MAX,public_config.manifest_url)==AM_DISABLED && opens==3 && sets==2);
    reset(&public_config);responses[0].status=302;responses[0].location="https://evil.test/p";
    assert(download(AM_MANIFEST_MAX,public_config.manifest_url)==AM_DISABLED && opens==1 && !sets);
    reset(&public_config);responses[0].status=302;responses[0].location="https://release-assets.githubusercontent.com:443/p";
    assert(download(AM_MANIFEST_MAX,public_config.manifest_url)==AM_DISABLED && !sets);
    reset(&local_config);responses[0]=(response_t){.status=200,.declared=AM_PAYLOAD_MAX,.actual=AM_PAYLOAD_MAX,.body=payload};
    assert(download(AM_PAYLOAD_MAX,local_config.payload_url)==AM_OK && reads==AM_PAYLOAD_MAX);
    reset(&local_config);responses[0].declared=AM_MANIFEST_MAX+1;assert(download(AM_MANIFEST_MAX,local_config.manifest_url)==AM_LIMIT && !reads);
    reset(&local_config);responses[0].declared=AM_SIGNATURE_MAX+1;assert(download(AM_SIGNATURE_MAX,local_config.signature_url)==AM_LIMIT && !reads);
    reset(&local_config);responses[0].declared=AM_PAYLOAD_MAX+1;assert(download(AM_PAYLOAD_MAX,local_config.payload_url)==AM_LIMIT && !reads);
    reset(&local_config);responses[0].actual=2;assert(download(AM_PAYLOAD_MAX,local_config.payload_url)==AM_IO);
    reset(&local_config);responses[0].chunked=true;assert(download(AM_PAYLOAD_MAX,local_config.payload_url)==AM_IO && !reads);
    reset(&local_config);responses[0].encoding="gzip";assert(download(AM_PAYLOAD_MAX,local_config.payload_url)==AM_IO && !reads);
    reset(&local_config);cancel_read=true;assert(download(AM_PAYLOAD_MAX,local_config.payload_url)==AM_CANCELLED && reads==1 && cleans==1);
    reset(&local_config);cancel_wifi=true;assert(download(AM_PAYLOAD_MAX,local_config.payload_url)==AM_CANCELLED && !opens);
    reset(&local_config);cancelled=true;assert(download(AM_PAYLOAD_MAX,local_config.payload_url)==AM_CANCELLED && !joins);
    am_source_config_t bad=local_config;bad.tls_ca_pem=NULL;reset(&bad);
    assert(download(AM_PAYLOAD_MAX,local_config.payload_url)==AM_DISABLED && !joins && !opens);
}
static void source_prepare(const char *id,unsigned abi,size_t size)
{
    source_phase=true;payload_size=size;
    int n=snprintf(manifest,sizeof manifest,"{\"schema\":1,\"id\":\"%s\",\"version\":\"1.0.0\",\"target\":\"esp32s3\",\"abi\":%u,\"sdk\":\"6.0.2\",\"loader\":\"2.1.0\",\"bytes\":%zu,\"sha256\":\"%064d\"}",id,abi,size,0);
    assert(n>0 && n<(int)sizeof manifest);
    responses[0]=(response_t){.status=200,.declared=(size_t)n,.actual=(size_t)n,.body=(const uint8_t *)manifest};
    responses[1]=(response_t){.status=200,.declared=sizeof signature,.actual=sizeof signature,.body=signature};
    responses[2]=(response_t){.status=200,.declared=size,.actual=size,.body=payload};
}
static am_result_t fetch(unsigned runtime_abi)
{
    am_https_t http;am_source_t source={.config=current,.bytes=&buffers,.valid=true};
    am_crypto_t crypto={.sha256=sha256,.verify_p256=verify};am_runtime_t runtime={"esp32s3","6.0.2","2.1.0",runtime_abi};
    am_result_t result=am_https_begin(&http,current,cancel,NULL,NULL);
    if(result==AM_OK)result=am_source_fetch(&source,&crypto,&runtime,am_https_get,&http,cancel,NULL);
    am_package_t cached;assert(am_source_cached(&source,&cached)==(result==AM_OK?AM_OK:AM_DISABLED));
    if(result==AM_OK)assert(cached.payload.size==payload_size && cached.payload.data==buffers.payload);
    else assert(!source.valid && !source.package.payload.data);
    am_https_end(&http);assert(!clients && offs==joins && ranges==offs);return result;
}
static void source(void)
{
    reset(&local_config);source_prepare("aliens",2,4928);assert(fetch(2)==AM_OK && opens==3 && verifies==2 && hashes==2 && natives==1);
    reset(&local_config);source_prepare("aliens",2,AM_PAYLOAD_MAX);assert(fetch(2)==AM_OK && opens==3 && natives==1);
    reset(&public_config);source_prepare("aliens",2,4928);assert(fetch(2)==AM_OK && opens==3 && natives==1);
    reset(&local_config);source_prepare("aliens",2,4928);verify_ok=false;
    assert(fetch(2)==AM_BAD_AUTH && opens==2 && verifies==1 && !hashes && !natives);
    reset(&local_config);source_prepare("snake",2,4928);assert(fetch(2)==AM_DISABLED && opens==2 && verifies==1 && !natives);
    reset(&local_config);source_prepare("counter",2,4928);assert(fetch(2)==AM_DISABLED && opens==2 && verifies==1 && !natives);
    reset(&local_config);source_prepare("aliens",1,4928);assert(fetch(2)==AM_DISABLED && opens==2 && verifies==1 && !natives);
    reset(&local_config);source_prepare("aliens",2,4928);assert(fetch(1)==AM_DISABLED && opens==2 && verifies==1 && !natives);
    reset(&local_config);source_prepare("aliens",2,4928);responses[2].declared=responses[2].actual=4927;
    assert(fetch(2)==AM_BAD_HASH && opens==3 && !natives);
    reset(&local_config);source_prepare("aliens",2,4928);native_result=AM_BAD_MANIFEST;
    assert(fetch(2)==AM_BAD_MANIFEST && opens==3 && natives==1);
}
static void readiness(void)
{
    assert(!am_https_clock_ready()); // Plausible RTC time without our sync is not recent.
    wall_clock=0;reset(&local_config);
    assert(am_https_prepare(current,cancel,NULL,progress)==AM_OK);
    assert(joins==1 && syncs==1 && waits==1 && stops==1 && offs==1 && ranges==1 && !opens);
    assert(phase_count==2 && !strcmp(phases[0],"Connecting to Wi-Fi...") && !strcmp(phases[1],"Syncing time..."));
    assert(am_https_clock_ready() && !connected);
    wall_clock+=10;reset(&local_config);
    assert(am_https_prepare(current,cancel,NULL,progress)==AM_OK);
    assert(!joins && !syncs && !offs && !ranges && !phase_count); // Prompt relaunch stays offline.
    connected=true;am_https_t http;
    assert(am_https_begin(&http,current,cancel,NULL,progress)==AM_OK);
    assert(!joins && !syncs && phase_count==1 && !strcmp(phases[0],"Downloading..."));
    am_https_end(&http);assert(offs==1 && ranges==1);
    wall_clock+=3601;reset(&local_config);
    assert(am_https_begin(&http,current,cancel,NULL,progress)==AM_OK);
    assert(joins==1 && syncs==1 && stops==1 && phase_count==3);
    assert(!strcmp(phases[2],"Downloading..."));
    uint8_t bytes[3];size_t size=0;
    assert(am_https_get(&http,current->manifest_url,bytes,sizeof bytes,&size)==AM_OK && size==3);
    assert(joins==1);am_https_end(&http);assert(offs==1 && ranges==1); // Same link through TLS.
    wall_clock=0;reset(&local_config);wifi_ok=false;
    assert(am_https_prepare(current,cancel,NULL,progress)==AM_WIFI_FAILED);
    assert(joins==1 && !syncs && offs==1 && ranges==1 && !opens);
    reset(&local_config);sync_ok=false;
    assert(am_https_prepare(current,cancel,NULL,progress)==AM_TIME_FAILED);
    assert(syncs==1 && stops==1 && waits==40 && offs==1 && ranges==1 && !opens && !am_https_clock_ready());
    reset(&local_config);cancel_sync=true;
    assert(am_https_prepare(current,cancel,NULL,progress)==AM_CANCELLED);
    assert(stops==1 && offs==1 && ranges==1 && !opens);
    puts("PASS readiness: missing/retained time, successful sync, recent offline relaunch, reused link, expired stamp, Wi-Fi failure, bounded sync timeout and cancellation cleanup; TLS verification remains enabled");
}
int main(int argc,char **argv)
{
    memset(payload,0xa5,sizeof payload);configs();
    if(!am_https_enabled()) {
        const am_source_config_t *profiles[]={&local_config,&public_config};
        for(unsigned i=0;i<2;i++) {
            reset(profiles[i]);am_https_t http;uint8_t out[4];size_t size=99;
            assert(am_https_begin(&http,current,cancel,NULL,NULL)==AM_DISABLED);
            assert(!http.radio_owned && !http.cancel);
            assert(am_https_get(&http,current->payload_url,out,sizeof out,&size)==AM_DISABLED && !size);
            am_https_end(&http);
            assert(!joins && !opens && !clients && !cleans && !offs && !ranges && !verifies && !hashes);
        }
        puts("PASS: absent SDK certificate-date checking disables public/local fetch before radio, HTTP, crypto or cleanup I/O");
        return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--readiness")){readiness();return 0;}
    transport();source();
    puts("PASS scoped local HTTPS/public redirects, certificate policy, exact payload caps, Aliens binding and authentication before payload (controlled doubles)");
}
