idf_component_register(SRCS "main.c" "settings.c" "reset.c"
                              "dimmer.c" "lamp_core.c" "lamp.c"
                              "gesture.c" "touch.c" "light_cmd.c"
                              "wifi_mgr.c" "captive.c" "web_server.c"
                              "time_mgr.c" "config_merge.c"
                              "ota.c" "ota_logic.c"
                              "schedule.c" "sched_mgr.c"
                              "mqtt_mgr.c" "ha_discovery.c"
                              "log_ring.c" "logbuf.c" "power.c"
                              "battery.c" "battery_logic.c"
                       INCLUDE_DIRS "."
                       REQUIRES appconf nvs_flash esp_driver_gpio
                              esp_driver_ledc esp_hw_support esp_system
                              esp_timer esp_wifi esp_netif esp_event
                              esp_http_server json spi_flash
                              app_update esp_app_format esp_https_ota
                              esp_http_client mbedtls mqtt esp-tls esp_pm)

# Bench build: an image that never confirms itself, to watch the rollback.
if(APP_OTA_TEST_NO_CONFIRM)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_OTA_TEST_NO_CONFIRM=1)
endif()

find_program(GZIP_EXECUTABLE gzip REQUIRED)
set(WEB_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../web")
set(WEB_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen_web")

foreach(page setup main info schedule)
    set(sym "${page}_html_gz")
    add_custom_command(
        OUTPUT ${WEB_GEN_DIR}/${page}.html.gz
        COMMAND ${CMAKE_COMMAND} -E make_directory ${WEB_GEN_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy ${WEB_SRC_DIR}/${page}.html
                ${WEB_GEN_DIR}/${page}.html
        COMMAND ${GZIP_EXECUTABLE} -9 -n -f ${WEB_GEN_DIR}/${page}.html
        DEPENDS ${WEB_SRC_DIR}/${page}.html)
    add_custom_command(
        OUTPUT ${WEB_GEN_DIR}/${page}_html.h
        COMMAND ${CMAKE_COMMAND}
                -DBIN=${WEB_GEN_DIR}/${page}.html.gz
                -DOUT=${WEB_GEN_DIR}/${page}_html.h
                -DSYM=${sym}
                -P ${CMAKE_CURRENT_LIST_DIR}/../cmake/bin2header.cmake
        DEPENDS ${WEB_GEN_DIR}/${page}.html.gz
                ${CMAKE_CURRENT_LIST_DIR}/../cmake/bin2header.cmake)
    list(APPEND WEB_GEN_HEADERS ${WEB_GEN_DIR}/${page}_html.h)
endforeach()

add_custom_target(web_assets DEPENDS ${WEB_GEN_HEADERS})
add_dependencies(${COMPONENT_LIB} web_assets)
target_include_directories(${COMPONENT_LIB} PRIVATE ${WEB_GEN_DIR})
