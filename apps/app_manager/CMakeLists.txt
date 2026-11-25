idf_component_register(SRCS "app_manager.c" "manager_ui.c" "home_apps.c" "manager_catalogue.c" "manager_assets.c" "manager_native_asset.c"
                           "controller.c" "controller_target.c" "target_profile.c"
                           "native_elf.c" "native_host.c" "native_video_target.c"
                           "native_codex.c" "native_codex_target.c"
                           "native_hackaday.c" "native_had_target.c" "native_had_http.c" "native_had_decode.c" "native_had_url.c"
                           "native_messages.c" "native_messages_target.c" "native_messages_portal.c"
                           "native_doom.c" "native_doom_target.c"
                           "package_source.c" "package_http.c" "package_config.c" "video_asset.c" "video_source.c"
                           "txn_json.c" "txn_package.c" "txn_store.c" "txn_vfs.c"
                           "txn_geometry.c" "txn_crypto_psa.c" "txn_littlefs.c"
                       PRIV_REQUIRES os espressif__elf_loader heap log mbedtls stb
                                     joltwallet__littlefs esp_partition spi_flash vfs freertos
                                     esp_http_client esp_http_server esp_wifi esp_timer esp_hw_support esp_netif esp_driver_usb_serial_jtag lwip)

set(BADGE_APP_MANAGER_CHANNEL "" CACHE FILEPATH "Generated public package channel header")
if(BADGE_APP_MANAGER_CHANNEL)
    configure_file("${BADGE_APP_MANAGER_CHANNEL}" "${CMAKE_CURRENT_BINARY_DIR}/am_package_channel.h" COPYONLY)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE AM_PACKAGE_CHANNEL_CONFIGURED=1)
endif()
