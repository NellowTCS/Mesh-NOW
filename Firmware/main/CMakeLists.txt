set(MESH_NOW_REQUIRES
    esp_common esp_wifi nvs_flash esp_timer Build driver)

# The built-in `json` component was removed in ESP-IDF 6.0
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    list(APPEND MESH_NOW_REQUIRES espressif__cjson esp_driver_uart)
else()
    list(APPEND MESH_NOW_REQUIRES json)
endif()

idf_component_register(SRCS "main.c"
                       "src/wifi_manager.c"
                       "src/serial_api.c"
                    INCLUDE_DIRS "."
                                 "include"
                    REQUIRES ${MESH_NOW_REQUIRES})