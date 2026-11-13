idf_component_register(SRCS "messaging.c" "message_portal.c"
                       PRIV_REQUIRES os esp_http_server esp_netif esp_wifi esp_timer esp_hw_support lwip)
