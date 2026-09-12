cmake_minimum_required(VERSION 3.16)

project(mesh_now_unit_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(MESH_NOW_TOP "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_library(mesh_now_mocks INTERFACE)
target_include_directories(mesh_now_mocks INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/mocks"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

set(MESH_SRCS
    "${MESH_NOW_TOP}/components/mesh_now/src/core/mesh_now.c"
    "${MESH_NOW_TOP}/components/mesh_now/src/core/mesh_now_pending.c"
    "${MESH_NOW_TOP}/components/mesh_now/src/net/mesh_now_peer.c"
    "${MESH_NOW_TOP}/components/mesh_now/src/net/mesh_now_route.c"
    "${MESH_NOW_TOP}/components/mesh_now/src/net/mesh_now_tx.c"
    "${MESH_NOW_TOP}/components/mesh_now/src/net/mesh_now_tasks.c"
    "${MESH_NOW_TOP}/components/mesh_now/src/net/mesh_now_retransmit.c"
    "${MESH_NOW_TOP}/components/mesh_now/src/codec/mesh_now_codec.c"
)

set(MPACK_DIR "${MESH_NOW_TOP}/components/mpack/mpack/src/mpack")
set(MPACK_SRCS
    "${MPACK_DIR}/mpack-common.c"
    "${MPACK_DIR}/mpack-expect.c"
    "${MPACK_DIR}/mpack-node.c"
    "${MPACK_DIR}/mpack-platform.c"
    "${MPACK_DIR}/mpack-reader.c"
    "${MPACK_DIR}/mpack-writer.c"
)

add_executable(mesh_now_unit_tests
    main.c
    support.c
    core_test.c
    route_test.c
    peer_test.c
    pending_test.c
    codec_test.c
    tx_test.c
    beacon_test.c
    ${MESH_SRCS}
    ${MPACK_SRCS}
)

target_include_directories(mesh_now_unit_tests PRIVATE
    "${MESH_NOW_TOP}/components/mesh_now/include"
    "${MESH_NOW_TOP}/components/mesh_now/src/core"
    "${MESH_NOW_TOP}/components/mesh_now/src/codec"
    "${MESH_NOW_TOP}/components/mpack/mpack/src/mpack"
)

target_link_libraries(mesh_now_unit_tests PRIVATE mesh_now_mocks)

target_compile_definitions(mesh_now_unit_tests PRIVATE _DARWIN_C_SOURCE)
target_compile_options(mesh_now_unit_tests PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-but-set-variable
)

enable_testing()
add_test(NAME mesh_now_unit_tests COMMAND mesh_now_unit_tests)
