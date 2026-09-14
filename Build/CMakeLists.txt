idf_component_register(SRCS
                       "src/core/mesh_now.c"
                       "src/core/mesh_now_pending.c"
                       "src/crypto/mesh_now_crypto.c"
                       "src/codec/mesh_now_codec.c"
                       "src/net/mesh_now_peer.c"
                       "src/net/mesh_now_recv.c"
                       "src/net/mesh_now_route.c"
                       "src/net/mesh_now_discovery.c"
                       "src/net/mesh_now_tx.c"
                       "src/net/mesh_now_tasks.c"
                       "src/net/mesh_now_retransmit.c"
                       "src/net/mesh_now_send.c"
                       "src/queue/message_queue.c"
                       "vendor/mpack/mpack/src/mpack/mpack-common.c"
                       "vendor/mpack/mpack/src/mpack/mpack-expect.c"
                       "vendor/mpack/mpack/src/mpack/mpack-node.c"
                       "vendor/mpack/mpack/src/mpack/mpack-platform.c"
                       "vendor/mpack/mpack/src/mpack/mpack-reader.c"
                       "vendor/mpack/mpack/src/mpack/mpack-writer.c" INCLUDE_DIRS
                       "include" PRIV_INCLUDE_DIRS
                       "src/core"
                       "vendor/mpack/mpack/src/mpack" REQUIRES esp_wifi esp_timer mbedtls)
