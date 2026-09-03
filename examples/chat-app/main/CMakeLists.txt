idf_component_register(SRCS "main.c"
                       "src/wifi_manager.c"
                       "src/serial_api.c"
                    INCLUDE_DIRS "."
                                 "include"
                    REQUIRES esp_common esp_wifi nvs_flash esp_timer mesh_now json driver)