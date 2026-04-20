idf_component_register(SRCS "src/mesh_now.c"
                       "src/message_queue.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_now esp_timer)
