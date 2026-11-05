execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    OUTPUT_VARIABLE GIT_VER
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_VER)
    set(GIT_VER "dev")
endif()

idf_component_register(SRCS "main.c" "settings.c" "reset.c"
                              "dimmer.c" "lamp_core.c" "lamp.c"
                              "gesture.c" "touch.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash esp_driver_gpio esp_driver_ledc
                              esp_hw_support esp_system esp_timer)

target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_VER="${GIT_VER}")
