#ifndef MESH_NOW_TEST_H
#define MESH_NOW_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"

extern int g_checks;
extern int g_failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_failures++;                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,        \
                    #cond);                                                \
        }                                                                  \
    } while (0)

#define CHECK_STREQ(a, b)                                                  \
    CHECK((a) != NULL && (b) != NULL && strcmp((a), (b)) == 0)

// MAC helpers for building fixtures.
void fill_mac(uint8_t mac[6], int seed);
bool macs_equal(const uint8_t a[6], const uint8_t b[6]);

// ESP-NOW mock observations (defined in support.c).
typedef struct {
    bool called;
    uint8_t dest[6];
    uint8_t data[512];
    size_t len;
    esp_err_t rc;
} espnow_send_t;

extern espnow_send_t g_send;
extern int g_send_calls;
extern int g_add_peer_calls;
extern esp_err_t g_add_peer_rc;
extern int g_del_peer_calls;
extern uint8_t g_last_del_peer[6];
extern int g_task_create_calls;
extern int g_esp_now_init_calls;
extern int g_esp_now_deinit_calls;
extern int g_esp_now_unreg_send_cb_calls;
extern int g_esp_now_unreg_recv_cb_calls;
extern bool g_is_peer_exist;
extern bool g_last_broadcast_added;
extern int g_request_route_calls;
extern int g_route_request_attempts;
extern int g_send_rerr_calls;

// Clock, RNG, and identity knobs.
extern int64_t g_now_us;
extern uint32_t g_random_state;
extern uint8_t g_local_mac[6];

// Reset all globals the tests and the mock own, including the mesh-now glue
// tables (they live in mesh_now.c) and the state mutex.
void test_reset_all(void);

// Suite entry points.
void run_core_tests(void);
void run_route_tests(void);
void run_peer_tests(void);
void run_pending_tests(void);
void run_codec_tests(void);
void run_tx_tests(void);
void run_beacon_tests(void);

#endif