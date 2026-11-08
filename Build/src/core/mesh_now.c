#include "mesh_now_internal.h"
#include "mesh_now_pool.h"
#include "message_queue.h"
#include <esp_log.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <string.h>

#define TAG "MESH_NOW"

mesh_now_pool_t peer_pool;
mesh_now_pool_t route_pool;
mesh_now_pool_t rreq_cache_pool;
mesh_now_pool_t route_request_pool;
mesh_now_pool_t pending_pool;
mesh_now_pool_t seen_id_pool;

int peer_count = 0;
int route_count = 0;
int route_request_count = 0;
uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = BROADCAST_MAC;
TaskHandle_t beacon_task_handle = NULL;
TaskHandle_t retransmit_task_handle = NULL;
mesh_now_receive_callback_t receive_callback = NULL;
mesh_now_route_failure_callback_t route_failure_callback = NULL;
bool encryption_enabled = false;
uint8_t encryption_key[MAX_ENCRYPTION_KEY];
size_t encryption_key_len = 0;
uint32_t next_message_id = 0;
uint8_t local_group_id = 0;
char local_node_name[MESH_NOW_NODE_NAME_MAX + 1] = {0};
SemaphoreHandle_t state_mutex = NULL;
int64_t time_offset_us = 0;

uint32_t mesh_now_generate_message_id(void)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    if (next_message_id == 0) {
        next_message_id = esp_random();
    }
    uint32_t id = next_message_id++;
    xSemaphoreGive(state_mutex);
    return id;
}

bool mesh_now_is_message_seen(uint32_t message_id)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    uint32_t *ids = (uint32_t *)seen_id_pool.blocks;
    for (size_t i = 0; i < seen_id_pool.used_count; ++i) {
        if (ids[i] == message_id) {
            xSemaphoreGive(state_mutex);
            return true;
        }
    }
    xSemaphoreGive(state_mutex);
    return false;
}

void mesh_now_mark_message_seen(uint32_t message_id)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    uint32_t *ids = (uint32_t *)seen_id_pool.blocks;
    for (size_t i = 0; i < seen_id_pool.used_count; ++i) {
        if (ids[i] == message_id) {
            xSemaphoreGive(state_mutex);
            return; // already present
        }
    }

    if (seen_id_pool.used_count < seen_id_pool.block_count) {
        uint32_t *slot = mesh_now_pool_alloc(&seen_id_pool);
        if (slot != NULL) {
            *slot = message_id;
        }
        xSemaphoreGive(state_mutex);
        return;
    }

    // Ring buffer: shift left and append at the end.
    memmove(&ids[0], &ids[1],
            (seen_id_pool.block_count - 1) * sizeof(uint32_t));
    ids[seen_id_pool.block_count - 1] = message_id;
    xSemaphoreGive(state_mutex);
}

void mesh_now_set_receive_callback(mesh_now_receive_callback_t callback)
{
    receive_callback = callback;
}

void mesh_now_set_route_failure_callback(mesh_now_route_failure_callback_t cb)
{
    route_failure_callback = cb;
}

void mesh_now_sync_time(uint32_t remote_timestamp)
{
    int64_t local_us = esp_timer_get_time();
    int64_t remote_us = (int64_t)remote_timestamp * 1000LL;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    if (time_offset_us == 0) {
        time_offset_us = local_us - remote_us;
    } else {
        int64_t new_offset = local_us - remote_us;
        time_offset_us = (time_offset_us + new_offset) / 2;
    }
    xSemaphoreGive(state_mutex);
}

uint32_t mesh_now_get_network_time_ms(void)
{
    int64_t local_us = esp_timer_get_time();
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    int64_t offset = time_offset_us;
    xSemaphoreGive(state_mutex);
    return (uint32_t)((local_us - offset) / 1000LL);
}

esp_err_t mesh_now_set_group(uint8_t group_id)
{
    local_group_id = group_id;
    return ESP_OK;
}

esp_err_t mesh_now_set_encryption_key(const uint8_t *key, size_t len)
{
    // AES-128-GCM requires exactly a 16-byte key.
    if (key == NULL || len != AES_GCM_KEY_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(encryption_key, key, len);
    encryption_key_len = len;
    encryption_enabled = true;
    return ESP_OK;
}

esp_err_t mesh_now_set_name(const char *name)
{
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // Safe without the mutex: app_main calls this before mesh_now_init()
    // creates state_mutex, and only the main task runs at that point.
    if (state_mutex != NULL) {
        xSemaphoreTake(state_mutex, portMAX_DELAY);
    }
    strncpy(local_node_name, name, MESH_NOW_NODE_NAME_MAX);
    local_node_name[MESH_NOW_NODE_NAME_MAX] = '\0';
    if (state_mutex != NULL) {
        xSemaphoreGive(state_mutex);
    }
    return ESP_OK;
}

const char *mesh_now_get_name(void)
{
    static char name_copy[MESH_NOW_NODE_NAME_MAX + 1];
    if (state_mutex != NULL) {
        xSemaphoreTake(state_mutex, portMAX_DELAY);
    }
    strncpy(name_copy, local_node_name, sizeof(name_copy));
    name_copy[MESH_NOW_NODE_NAME_MAX] = '\0';
    if (state_mutex != NULL) {
        xSemaphoreGive(state_mutex);
    }
    return name_copy;
}

int mesh_now_get_peer_count(void)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    int count = peer_count;
    xSemaphoreGive(state_mutex);
    return count;
}

mesh_peer_t *mesh_now_get_peers(void)
{
    return (mesh_peer_t *)peer_pool.blocks;
}

bool mesh_now_is_encrypted(void)
{
    return encryption_enabled;
}

uint8_t mesh_now_get_group_id(void)
{
    return local_group_id;
}

bool mesh_now_peer_is_online(const mesh_peer_t *peer)
{
    if (peer == NULL) {
        return false;
    }
    // Read both fields under the lock so an expiry in the beacon task cannot
    // race with this check.
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    bool active = peer->active;
    int64_t last_seen = peer->last_seen;
    xSemaphoreGive(state_mutex);
    if (!active) {
        return false;
    }
    return (esp_timer_get_time() - last_seen) < PEER_EXPIRY_US;
}

int mesh_now_snapshot_peers(mesh_peer_t *out, size_t max_out)
{
    if (out == NULL || max_out == 0) {
        return 0;
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    mesh_peer_t *peer_blocks = (mesh_peer_t *)peer_pool.blocks;
    size_t to_copy = peer_count < (int)max_out ? peer_count : max_out;
    size_t copied = 0;
    for (size_t i = 0; i < peer_pool.block_count && copied < to_copy; i++) {
        if (peer_blocks[i].active) {
            out[copied++] = peer_blocks[i];
        }
    }
    int count = (int)copied;
    xSemaphoreGive(state_mutex);
    return count;
}

esp_err_t mesh_now_init(void)
{
    ESP_LOGI(TAG, "Initializing ESP-NOW mesh networking");

    state_mutex = xSemaphoreCreateMutex();
    if (state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_FAIL;
    }

    // Create all fixed-block pools from the heap
    esp_err_t err;

    err = mesh_now_pool_create(&peer_pool, sizeof(mesh_peer_t), MAX_PEERS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create peer pool");
        return err;
    }

    err = mesh_now_pool_create(&route_pool, sizeof(mesh_route_t), MAX_ROUTES);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create route pool");
        mesh_now_pool_destroy(&peer_pool);
        return err;
    }

    err = mesh_now_pool_create(&rreq_cache_pool, sizeof(rreq_cache_entry_t),
                               MAX_RREQ_CACHE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RREQ cache pool");
        mesh_now_pool_destroy(&route_pool);
        mesh_now_pool_destroy(&peer_pool);
        return err;
    }

    err = mesh_now_pool_create(&route_request_pool, sizeof(route_request_t),
                               MAX_ROUTE_REQUESTS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create route-request pool");
        mesh_now_pool_destroy(&rreq_cache_pool);
        mesh_now_pool_destroy(&route_pool);
        mesh_now_pool_destroy(&peer_pool);
        return err;
    }

    err = mesh_now_pool_create(&pending_pool, sizeof(pending_message_t),
                               MAX_PENDING_MESSAGES);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create pending pool");
        mesh_now_pool_destroy(&route_request_pool);
        mesh_now_pool_destroy(&rreq_cache_pool);
        mesh_now_pool_destroy(&route_pool);
        mesh_now_pool_destroy(&peer_pool);
        return err;
    }

    err = mesh_now_pool_create(&seen_id_pool, sizeof(uint32_t),
                               MAX_SEEN_MESSAGE_IDS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create seen-ID pool");
        mesh_now_pool_destroy(&pending_pool);
        mesh_now_pool_destroy(&route_request_pool);
        mesh_now_pool_destroy(&rreq_cache_pool);
        mesh_now_pool_destroy(&route_pool);
        mesh_now_pool_destroy(&peer_pool);
        return err;
    }

    message_queue_init();

    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW: %s", esp_err_to_name(ret));
        return ret;
    }

    mesh_now_register_callbacks();

    esp_now_peer_info_t broadcast_peer;
    memset(&broadcast_peer, 0, sizeof(esp_now_peer_info_t));
    memcpy(broadcast_peer.peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
    broadcast_peer.channel = WIFI_CHANNEL;
    broadcast_peer.encrypt = false;

    ret = esp_now_add_peer(&broadcast_peer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add broadcast peer: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = mesh_now_start_tasks();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "ESP-NOW mesh networking initialized successfully");
    return ESP_OK;
}

esp_err_t mesh_now_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing ESP-NOW mesh networking");

    if (beacon_task_handle != NULL) {
        vTaskDelete(beacon_task_handle);
        beacon_task_handle = NULL;
    }

    if (retransmit_task_handle != NULL) {
        vTaskDelete(retransmit_task_handle);
        retransmit_task_handle = NULL;
    }

    esp_now_del_peer(broadcast_mac);
    esp_now_unregister_send_cb();
    esp_now_unregister_recv_cb();

    esp_err_t ret = esp_now_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize ESP-NOW: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    peer_count = 0;

    mesh_now_pool_destroy(&peer_pool);
    mesh_now_pool_destroy(&route_pool);
    mesh_now_pool_destroy(&rreq_cache_pool);
    mesh_now_pool_destroy(&route_request_pool);
    mesh_now_pool_destroy(&pending_pool);
    mesh_now_pool_destroy(&seen_id_pool);

    if (state_mutex != NULL) {
        vSemaphoreDelete(state_mutex);
        state_mutex = NULL;
    }

    ESP_LOGI(TAG, "ESP-NOW mesh networking deinitialized successfully");
    return ESP_OK;
}