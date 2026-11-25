#define MESSAGES_HOST_TEST 1
#define app_main messages_test_main
#include "../modules/messaging/src/messages_module.c"
#include "native_host_double.h"
void messages_test_memory(void){nh_memory(&session,sizeof session,NH_PSRAM,true);}
