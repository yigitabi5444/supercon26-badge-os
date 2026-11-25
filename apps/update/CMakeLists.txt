idf_component_register(SRCS "update.c" "ota.c" "update_layout.c"
                       PRIV_REQUIRES os cricket app_update bootloader_support esp_app_format esp_http_client
                                     esp_https_ota esp_timer mbedtls spi_flash)
