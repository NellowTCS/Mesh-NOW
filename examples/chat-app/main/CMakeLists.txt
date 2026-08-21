idf_component_register(SRCS "main.c"
                       "src/wifi_manager.c"
                       "src/web_server.c"
                    INCLUDE_DIRS "."
                                 "include"
                    REQUIRES esp_common esp_http_server esp_wifi nvs_flash esp_timer mesh_now)