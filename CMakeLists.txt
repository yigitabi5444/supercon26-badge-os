cmake_minimum_required(VERSION 3.16)
option(BADGE_APP_MANAGER "Build Core14 with separately installed apps" ON)
if(BADGE_APP_MANAGER)
    add_compile_definitions(BADGE_APP_MANAGER=1)
    if(NOT DEFINED SDKCONFIG_DEFAULTS)
        set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
    endif()
    list(APPEND SDKCONFIG_DEFAULTS
         "${CMAKE_SOURCE_DIR}/apps/app_manager/sdkconfig.defaults"
         "${CMAKE_SOURCE_DIR}/apps/app_manager/sdkconfig.store.defaults"
         "${CMAKE_SOURCE_DIR}/sdkconfig.app-manager.defaults")
    set(COMPONENTS main os badge clock clock_faces update pong snake life blocks radar source
                   weather settings faq app_manager esp_psram espcoredump)
else()
    add_compile_definitions(BADGE_APP_MANAGER=0)
    list(APPEND EXCLUDE_COMPONENTS app_manager clock_faces)
endif()
set(EXTRA_COMPONENT_DIRS apps)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(badge_os)

if(BADGE_APP_MANAGER)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/empty-appstore")
    littlefs_create_partition_image(appstore "${CMAKE_BINARY_DIR}/empty-appstore")
endif()
