// Host-test implementation backing every mocked ESP-IDF symbol the mesh-now
// sources reference, plus the deterministic knobs the suites drive. The
// radio, scheduler, and AES-GCM accelerator are intentionally absent: these
// stubs only let framing, tables, and state machinery run on the host.

#include <string.h>

#include <esp_mac.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_timer.h>

#include "mesh_now_internal.h"
#include "message_queue.h"
#include "test.h"

static int mutex_slot;

// test knobs

int g_checks = 0;
int g_failures = 0;

espnow_send_t g_send;
int g_send_calls = 0;
int g_add_peer_calls = 0;
esp_err_t g_add_peer_rc = ESP_OK;
int g_del_peer_calls = 0;
uint8_t g_last_del_peer[ESP_NOW_ETH_ALEN] = {0};
int g_task_create_calls = 0;
int g_esp_now_init_calls = 0;
int g_esp_now_deinit_calls = 0;
bool g_is_peer_exist = false;
bool g_last_broadcast_added = false;
int g_request_route_calls = 0;
int g_route_request_attempts = 0;
int g_send_rerr_calls = 0;

int64_t g_now_us = 0;
uint32_t g_random_state = 0x12345678u;
uint8_t g_local_mac[ESP_NOW_ETH_ALEN] = {0xdc, 0xfe, 0x01, 0x02, 0x03, 0x04};

const uint8_t g_broadcast_ff[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff,
                                                  0xff, 0xff};

// FreeRTOS mocks

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    // Single-threaded tests: one shared no-op mutex.
    return (SemaphoreHandle_t)&mutex_slot;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xBlockTime)
{
    (void)xSemaphore;
    (void)xBlockTime;
    return pdPASS;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
    (void)xSemaphore;
    return pdPASS;
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
    (void)xSemaphore;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode,
                                   const char *pcName,
                                   configSTACK_DEPTH_TYPE usStackDepth,
                                   void *pvParameters, UBaseType_t uxPriority,
                                   TaskHandle_t *pvCreatedTask,
                                   BaseType_t xCoreID)
{
    (void)pvTaskCode;
    (void)pcName;
    (void)usStackDepth;
    (void)pvParameters;
    (void)uxPriority;
    (void)xCoreID;
    g_task_create_calls++;
    static int task_slot;
    if (pvCreatedTask != NULL) {
        *pvCreatedTask = (TaskHandle_t)&task_slot;
    }
    return pdPASS;
}

void vTaskDelete(TaskHandle_t xTaskToDelete)
{
    (void)xTaskToDelete;
}

void vTaskDelay(TickType_t xTicksToDelay)
{
    (void)xTicksToDelay;
}

// ESP-IDF stubs

const char *esp_err_to_name(esp_err_t code)
{
    (void)code;
    return "mock-err";
}

esp_err_t esp_now_init(void)
{
    g_esp_now_init_calls++;
    return ESP_OK;
}

esp_err_t esp_now_deinit(void)
{
    g_esp_now_deinit_calls++;
    return ESP_OK;
}

esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer)
{
    g_add_peer_calls++;
    if (peer != NULL && macs_equal(peer->peer_addr, g_broadcast_ff)) {
        g_last_broadcast_added = true;
    }
    return g_add_peer_rc;
}

esp_err_t esp_now_del_peer(const uint8_t *mac_addr)
{
    g_del_peer_calls++;
    if (mac_addr != NULL) {
        memcpy(g_last_del_peer, mac_addr, ESP_NOW_ETH_ALEN);
    }
    return ESP_OK;
}

esp_err_t esp_now_unregister_send_cb(void)
{
    return ESP_OK;
}

esp_err_t esp_now_unregister_recv_cb(void)
{
    return ESP_OK;
}

bool esp_now_is_peer_exist(const uint8_t *mac_addr)
{
    (void)mac_addr;
    return g_is_peer_exist;
}

esp_err_t esp_now_send(const uint8_t *mac_addr, const uint8_t *data,
                       size_t data_len)
{
    g_send.called = true;
    g_send_calls++;
    memcpy(g_send.dest, mac_addr, ESP_NOW_ETH_ALEN);
    if (data_len <= sizeof(g_send.data)) {
        memcpy(g_send.data, data, data_len);
    }
    g_send.len = data_len;
    return g_send.rc;
}

esp_err_t esp_read_mac(uint8_t mac[6], esp_mac_type_t type)
{
    (void)type;
    memcpy(mac, g_local_mac, ESP_NOW_ETH_ALEN);
    return ESP_OK;
}

uint32_t esp_random(void)
{
    uint32_t current = g_random_state;
    g_random_state = g_random_state * 1664525u + 1013904223u;
    return current;
}

int64_t esp_timer_get_time(void)
{
    return g_now_us;
}

//  mesh-now coverage shims

void mesh_now_register_callbacks(void)
{
}

esp_err_t message_queue_init(void)
{
    return ESP_OK;
}

esp_err_t mesh_now_request_route(const uint8_t *target)
{
    (void)target;
    g_request_route_calls++;
    return ESP_OK;
}

int mesh_now_route_request_attempts(const uint8_t *target)
{
    (void)target;
    return g_route_request_attempts;
}

void mesh_now_send_rerr(void)
{
    g_send_rerr_calls++;
}

void mesh_now_expire_route_requests(int64_t now_us)
{
    (void)now_us;
}

size_t mesh_now_build_aad(const mesh_message_t *msg, uint8_t aad[32])
{
    memset(aad, 0, 32);
    aad[0] = msg->type;
    aad[1] = msg->group_id;
    aad[2] = (uint8_t)msg->timestamp;
    aad[3] = (uint8_t)(msg->timestamp >> 8);
    aad[4] = (uint8_t)(msg->timestamp >> 16);
    aad[5] = (uint8_t)(msg->timestamp >> 24);
    return 32;
}

// Permutation cipher
void mesh_now_build_nonce(uint32_t message_id, const uint8_t *sender_mac,
                          uint8_t nonce[AES_GCM_NONCE_LEN])
{
    memset(nonce, 0, AES_GCM_NONCE_LEN);
    for (size_t i = 0; i < AES_GCM_NONCE_LEN && i < sizeof(message_id); i++) {
        nonce[i] = (uint8_t)(message_id >> (8 * i));
    }
    for (size_t i = 0; i < (AES_GCM_NONCE_LEN - sizeof(message_id)) &&
                    i < ESP_NOW_ETH_ALEN;
         i++) {
        nonce[sizeof(message_id) + i] = sender_mac[i];
    }
}

esp_err_t mesh_now_aes_gcm_encrypt(const uint8_t *plaintext, size_t pt_len,
                                   const uint8_t *aad, size_t aad_len,
                                   const uint8_t key[16],
                                   const uint8_t nonce[AES_GCM_NONCE_LEN],
                                   uint8_t *ciphertext,
                                   uint8_t tag[AES_GCM_TAG_LEN])
{
    (void)aad;
    (void)aad_len;
    (void)key;
    (void)nonce;
    memcpy(ciphertext, plaintext, pt_len);
    memset(tag, 0, AES_GCM_TAG_LEN);
    return ESP_OK;
}

esp_err_t mesh_now_aes_gcm_decrypt(const uint8_t *ciphertext, size_t ct_len,
                                   const uint8_t *aad, size_t aad_len,
                                   const uint8_t key[16],
                                   const uint8_t nonce[AES_GCM_NONCE_LEN],
                                   const uint8_t tag[AES_GCM_TAG_LEN],
                                   uint8_t *plaintext)
{
    (void)aad;
    (void)aad_len;
    (void)key;
    (void)nonce;
    (void)tag;
    memcpy(plaintext, ciphertext, ct_len);
    return ESP_OK;
}

void fill_mac(uint8_t mac[6], int seed)
{
    mac[0] = 0xaa;
    mac[1] = 0xbb;
    mac[2] = (uint8_t)(seed & 0xff);
    mac[3] = (uint8_t)((seed >> 8) & 0xff);
    mac[4] = (uint8_t)((seed >> 16) & 0xff);
    mac[5] = (uint8_t)((seed >> 24) & 0xff);
}

bool macs_equal(const uint8_t a[6], const uint8_t b[6])
{
    return memcmp(a, b, ESP_NOW_ETH_ALEN) == 0;
}

void test_reset_all(void)
{
    memset(&g_send, 0, sizeof(g_send));
    g_send.rc = ESP_OK;
    g_send_calls = 0;
    g_add_peer_calls = 0;
    g_add_peer_rc = ESP_OK;
    g_del_peer_calls = 0;
    memset(g_last_del_peer, 0, sizeof(g_last_del_peer));
    g_task_create_calls = 0;
    g_esp_now_init_calls = 0;
    g_esp_now_deinit_calls = 0;
    g_is_peer_exist = false;
    g_last_broadcast_added = false;
    g_request_route_calls = 0;
    g_route_request_attempts = 0;
    g_send_rerr_calls = 0;

    g_now_us = 1000000; // 1s, a quiet baseline for equality-checked logic
    g_random_state = 0x12345678u;
    g_local_mac[0] = 0xdc;
    g_local_mac[1] = 0xfe;
    g_local_mac[2] = 0x01;
    g_local_mac[3] = 0x02;
    g_local_mac[4] = 0x03;
    g_local_mac[5] = 0x04;

    peer_count = 0;
    memset(peers, 0, sizeof(peers));
    route_count = 0;
    memset(routes, 0, sizeof(routes));
    memset(rreq_cache, 0, sizeof(rreq_cache));
    memset(route_requests, 0, sizeof(route_requests));
    memset(pending_messages, 0, sizeof(pending_messages));
    seen_message_count = 0;
    memset(seen_message_ids, 0, sizeof(seen_message_ids));
    next_message_id = 0;
    local_group_id = 0;
    local_node_name[0] = '\0';
    encryption_enabled = false;
    encryption_key_len = 0;
    memset(encryption_key, 0, sizeof(encryption_key));
    time_offset_us = 0;
    state_mutex = NULL;
    beacon_task_handle = NULL;
    retransmit_task_handle = NULL;
    memset(broadcast_mac, 0xff, sizeof(broadcast_mac));
}