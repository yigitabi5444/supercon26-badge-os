// Compile original resident handlers; expose identity checks only to this host fixture.
#include <stdint.h>
void ranging_me(uint8_t [6]);
void badge_id(char [5]);
void wifi_on(void);
#include "../native_messages_portal.c"
bool messages_test_handler(esp_err_t (*fn)(httpd_req_t *))
{return fn==page || fn==state || fn==action;}
bool messages_test_dns(void (*entry)(void *)){return entry==dns;}
