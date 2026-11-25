// Reuse count-only platform doubles, not the old guard main/vector execution.
#define main unused_guard_vectors
#include "test_littlefs_guard.c"
#undef main
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "../controller_target.h"
#include "../package_http.h"
#include "esp_netif_sntp.h"
#include "../txn_crypto_psa.h"
#include "../native_video.h"
#include "../native_codex.h"
#include "../native_hackaday.h"
#include "../native_messages.h"
#include "../native_doom.h"
const am_native_messages_t am_native_messages_target={0};
am_result_t app_manager_launch_messages_snapshot(const uint8_t *copy,size_t size,const am_metadata_t *meta,const am_native_messages_t *v)
{(void)copy;(void)size;(void)meta;(void)v;assert(0);return AM_IO;}
const am_native_hackaday_t am_native_hackaday_target={0};
am_result_t app_manager_launch_hackaday_snapshot(const uint8_t *copy,size_t size,const am_metadata_t *meta,const am_native_hackaday_t *v)
{(void)copy;(void)size;(void)meta;(void)v;assert(0);return AM_IO;}
const am_native_codex_t am_native_codex_target={0};
am_result_t app_manager_launch_codex_snapshot(const uint8_t *copy,size_t size,const am_metadata_t *meta,const am_native_codex_t *v)
{(void)copy;(void)size;(void)meta;(void)v;assert(0);return AM_IO;}

static bool deferred,request;
static unsigned allocations,adapter_setup_calls,verify_calls,launches,quiesces,ends,network_calls;
void btn_home_prompt(bool value){deferred=value;request=false;}
bool btn_home_requested(void){bool value=request;request=false;return value;}
#ifdef PROVIDE_HOME_END
bool btn_home_prompt_end(void)
{ bool value=request;request=false;deferred=false;ends++;return value; }
#endif
void *heap_caps_aligned_alloc(size_t a,size_t size,unsigned caps)
{ (void)a;(void)size;(void)caps;allocations++;return NULL; }
void heap_caps_free(void *memory){assert(!memory);}
static am_result_t no_verify(void *context,const uint8_t *bytes,size_t size,const uint8_t signature[64])
{ (void)context;(void)bytes;(void)size;(void)signature;verify_calls++;assert(0);return AM_BAD_AUTH; }
am_result_t am_psa_adapter(am_psa_context_t *ctx,const uint8_t *key,size_t size,am_crypto_t *out)
{
    assert(ctx && key && size==65 && key[0]==4 && out);adapter_setup_calls++;
    // Like the real adapter, setup only copies public bytes and binds callbacks.
    memcpy(ctx->public_key,key,size);*out=(am_crypto_t){ctx,hash,no_verify};return AM_OK;
}
am_result_t app_manager_launch_snapshot(const uint8_t *copy,size_t size,const am_metadata_t *meta)
{ (void)copy;(void)size;(void)meta;launches++;assert(0);return AM_IO; }
am_result_t app_manager_quiesce_snapshot(void)
{ quiesces++;assert(0);return AM_IO; }
am_result_t am_psa_sha256_stream(am_reader_t read,void *ctx,uint32_t size,uint8_t out[32])
{(void)read;(void)ctx;(void)size;(void)out;assert(0);return AM_IO;}
am_result_t app_manager_launch_video_snapshot(const uint8_t *copy,size_t size,const am_metadata_t *meta,const am_native_video_t *v)
{(void)copy;(void)size;(void)meta;(void)v;assert(0);return AM_IO;}
int32_t am_native_video_inflate(uint8_t *out,uint32_t n,const uint8_t *in,uint32_t size)
{(void)out;(void)n;(void)in;(void)size;assert(0);return -1;}
bool am_native_video_info(void){assert(0);return false;}

// The real HTTP source is linked. Factory/profile checks must never reach these SDK calls.
static void no_network(void){network_calls++;assert(0);}
int64_t esp_timer_get_time(void){no_network();return 0;}
bool wifi_connect(int timeout){(void)timeout;no_network();return false;}
bool wifi_connected(void){no_network();return false;}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c){(void)c;no_network();return ESP_FAIL;}
esp_err_t esp_netif_sntp_sync_wait(TickType_t t){(void)t;no_network();return ESP_FAIL;}
void esp_netif_sntp_deinit(void){no_network();}
void wifi_off(void){no_network();}
void ranging_start(void){no_network();}
esp_err_t esp_crt_bundle_attach(void *config){(void)config;no_network();return ESP_FAIL;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{ (void)config;no_network();return NULL; }
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client,int timeout)
{ (void)client;(void)timeout;no_network();return ESP_FAIL; }
esp_err_t esp_http_client_open(esp_http_client_handle_t client,int size)
{ (void)client;(void)size;no_network();return ESP_FAIL; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client)
{ (void)client;no_network();return -1; }
int esp_http_client_get_status_code(esp_http_client_handle_t client)
{ (void)client;no_network();return 0; }
bool esp_http_client_is_chunked_response(esp_http_client_handle_t client)
{ (void)client;no_network();return false; }
int esp_http_client_read(esp_http_client_handle_t client,char *bytes,int size)
{ (void)client;(void)bytes;(void)size;no_network();return -1; }
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client)
{ (void)client;no_network();return false; }
esp_err_t esp_http_client_close(esp_http_client_handle_t client)
{ (void)client;no_network();return ESP_FAIL; }
esp_err_t esp_http_client_set_url(esp_http_client_handle_t client,const char *url)
{ (void)client;(void)url;no_network();return ESP_FAIL; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{ (void)client;no_network();return ESP_FAIL; }

static unsigned aliens_index(void)
{
    unsigned selected=AM_COUNT,native_count=0;
    for(unsigned i=0;i<AM_COUNT;i++) {
        assert(strcmp(am_catalogue[i].id,"counter"));
        if(am_catalogue[i].native)native_count++;
        if(!strcmp(am_catalogue[i].id,"aliens"))selected=i;
    }
    assert(selected<AM_COUNT && native_count==7 && am_catalogue[selected].native);
    assert(!strcmp(am_catalogue[selected].name,"SpaceInv"));return selected;
}
static void reset_counts(void)
{
    (void)reset();allocations=adapter_setup_calls=verify_calls=launches=quiesces=ends=network_calls=0;
    deferred=request=false;
}
static void zero_io(unsigned setup)
{
    assert(!reads && !physical_calls && !configured_calls && !mounts && !writes && !unmounts);
    assert(!allocations && adapter_setup_calls==setup && !verify_calls && !network_calls && !launches && !quiesces);
    assert(!deferred);
}
static void disabled_mutations(am_controller_t *controller,unsigned selected)
{
    assert(am_controller_action(controller,selected,AM_INSTALL)==AM_DISABLED);
    assert(am_controller_action(controller,selected,AM_OPEN)==AM_DISABLED);
    assert(am_controller_action(controller,selected,AM_UPDATE)==AM_DISABLED);
    assert(am_controller_action(controller,selected,AM_UNINSTALL)==AM_DISABLED);
}
static void default_profile(unsigned selected)
{
    reset_counts();am_controller_t controller;
#if CONFIG_BADGE_APP_MANAGER_FRESH_LAYOUT
    static const uint8_t digest[]={
        0x56,0xf1,0x79,0x89,0x31,0xb2,0x0c,0x4f,0x3d,0x4f,0x7f,0x21,0x8c,0x9d,0x41,0x31,
        0x90,0x68,0x16,0x77,0x18,0xc2,0x58,0x6a,0x12,0x9a,0x7d,0x33,0x6c,0x62,0x62,0xe0};
    assert(am_app_layout_policy.qualified && am_app_layout_policy.address==0x3c0000 &&
           am_app_layout_policy.size==0x260000 && am_app_layout_policy.flash_size==0x800000 &&
           am_app_layout_policy.reserve_bytes==20480 && !memcmp(am_app_layout_policy.table_sector_sha256,digest,32));
#else
    assert(!am_app_layout_policy.qualified && !am_app_layout_policy.address && !am_app_layout_policy.size);
#endif
    assert(!am_video_source_config.code && !am_video_source_config.receipt_url &&
           !am_video_source_config.signature_url && !am_video_source_config.clip_url &&
           !am_video_source_valid(&am_video_source_config));
    assert(!am_app_source_config.public_key && !am_app_source_config.key_size &&
           !am_app_source_config.manifest_url && !am_app_source_config.signature_url && !am_app_source_config.payload_url);
    assert(am_target_start(NULL)==AM_IO);
    assert(am_target_start_with_source(NULL,NULL)==AM_IO);
    assert(am_target_start_with_profile(NULL,NULL,NULL)==AM_IO);zero_io(0);
    assert(am_target_start(&controller)==AM_DISABLED);
    assert(controller.state==AM_UNASSIGNED && !controller.store.work && !controller.execution_copy);
    assert(!controller.platform.assigned && !controller.platform.source_ready && !strcmp(controller.store.app_id,"aliens"));
    assert(controller.store.runtime.abi==2);zero_io(0);disabled_mutations(&controller,selected);
    assert(am_controller_action(&controller,selected,AM_FETCH)==AM_DISABLED);
    for(unsigned i=0;i<AM_COUNT;i++) {
        assert(!controller.local[i].installed && !controller.local[i].fetch_available);
        assert(am_controller_action(&controller,i,AM_DETAILS)==AM_OK);
        am_action_t actions[4];size_t n=am_actions(i,&controller.local[i],false,actions);
        assert(n==2 && actions[n-1].kind==AM_DETAILS);
        assert(actions[0].kind==AM_INSTALL && !actions[0].enabled);
    }
    assert(am_controller_shutdown(&controller)==AM_OK);zero_io(0);
#ifdef PROVIDE_HOME_END
    assert(ends==2);
#else
    assert(ends==0);
#endif
}
static void unassigned_sources(unsigned selected)
{
    static const uint8_t public_bytes[65]={4}; // Synthetic public descriptor; never imported or verified.
    const am_source_config_t key_only={.public_key=public_bytes,.key_size=sizeof public_bytes};
    am_controller_t controller;reset_counts();assert(!am_source_config_valid(&key_only));
    assert(am_target_start_with_source(&controller,&key_only)==AM_DISABLED);
    assert(controller.state==AM_UNASSIGNED && !controller.platform.assigned && !controller.platform.source_ready);
    assert(!controller.store.work && !controller.local[selected].fetch_available);zero_io(1);
    disabled_mutations(&controller,selected);assert(am_controller_action(&controller,selected,AM_FETCH)==AM_DISABLED);
    assert(am_controller_shutdown(&controller)==AM_OK);zero_io(1);

    const am_source_config_t source={.public_key=public_bytes,.key_size=sizeof public_bytes,
        .manifest_url="https://packages.example/manifest.json",.signature_url="https://packages.example/manifest.sig",
        .payload_url="https://packages.example/module.elf"};
    am_layout_policy_t unqualified=reset();unqualified.qualified=false;reset_counts();
    assert(am_source_config_valid(&source));
    assert(am_target_start_with_profile(&controller,&source,&unqualified)==AM_DISABLED);
    assert(controller.state==AM_UNASSIGNED && !controller.platform.assigned && controller.platform.source_ready==am_https_enabled());
    assert(!controller.store.work);zero_io(1);disabled_mutations(&controller,selected);
    for(unsigned i=0;i<AM_COUNT;i++)if(i!=selected)assert(!controller.local[i].fetch_available);
    am_action_t actions[4];size_t n=am_actions(selected,&controller.local[selected],false,actions);
    assert(n==2 && actions[1].kind==AM_DETAILS);
#ifdef PROVIDE_HOME_END
    assert(controller.local[selected].fetch_available==am_https_enabled());
    assert(actions[0].kind==AM_INSTALL && !actions[0].enabled);
    if(!am_https_enabled()) {
        assert(am_controller_action(&controller,selected,AM_FETCH)==AM_DISABLED);
        zero_io(1);
    }
#else
    assert(!controller.local[selected].fetch_available && actions[0].kind==AM_INSTALL && !actions[0].enabled);
    assert(am_controller_action(&controller,selected,AM_FETCH)==AM_DISABLED);
#endif
    // Merely configuring a source never fetches bytes or qualifies a layout.
    assert(am_controller_shutdown(&controller)==AM_OK);zero_io(1);

    reset_counts();
    assert(am_target_start_with_apps(&controller,NULL,NULL,NULL,&source,&unqualified)==AM_DISABLED);
    assert(controller.state==AM_UNASSIGNED && !controller.store.work);
    for(unsigned i=0;i<AM_COUNT;i++)assert(!controller.local[i].fetch_available);
    bool ready=false;am_store_t routed={0};
    assert(controller.platform.select(controller.platform.context,&routed,"codex_board",false,&ready)==AM_OK);
    assert(ready==am_https_enabled()); // Configuration routes correctly without assigning storage.
    assert(am_controller_shutdown(&controller)==AM_OK);zero_io(1);
    reset_counts();
    assert(am_target_start_with_downloads(&controller,NULL,NULL,NULL,NULL,&source,&unqualified)==AM_DISABLED);
    assert(controller.state==AM_UNASSIGNED && !controller.store.work);
    ready=false;
    assert(controller.platform.select(controller.platform.context,&routed,"hackaday",false,&ready)==AM_OK);
    assert(ready==am_https_enabled());
    assert(am_controller_shutdown(&controller)==AM_OK);zero_io(1);
}
int main(void)
{
    unsigned selected=aliens_index();
    am_local_t rows[AM_COUNT]={0};unsigned order[AM_COUNT];rows[selected].installed=true;am_order(rows,order);
    assert(order[AM_COUNT-1]==selected && aliens_index()==selected);
    default_profile(selected);unassigned_sources(selected);
    puts("PASS actual native target factory: stable SpaceInv identity, default zero flash/network/crypto setup/allocations, key-only nonassignment, unqualified source zero I/O, helper boundary and native launch signatures; count-only platform doubles.");
    return 0;
}

am_result_t am_native_doom_target_launch(am_store_t *s,const am_lease_t *l,const am_metadata_t *m,const char *p)
{(void)s;(void)l;(void)m;(void)p;assert(0);return AM_DISABLED;}
