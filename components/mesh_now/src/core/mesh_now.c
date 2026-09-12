#include "mesh_now_internal.h"
#include "message_queue.h"
#include <esp_log.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <string.h>

#define TAG "MESH_NOW"

mesh_peer_t peers[MAX_PEERS];
int peer_count = 0;
mesh_route_t routes[MAX_ROUTES];
int route_count = 0;
rreq_cache_entry_t rreq_cache[MAX_RREQ_CACHE];
route_request_t route_requests[MAX_ROUTE_REQUESTS];
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
pending_message_t pending_messages[MAX_PENDING_MESSAGES];
uint32_t seen_message_ids[MAX_SEEN_MESSAGE_IDS];
int seen_message_count = 0;
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
    for (int i = 0; i < seen_message_count; ++i) {
        if (seen_message_ids[i] == message_id) {
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
    if (seen_message_count < MAX_SEEN_MESSAGE_IDS) {
        seen_message_ids[seen_message_count++] = message_id;
        xSemaphoreGive(state_mutex);
        return;
    }

    memmove(&seen_message_ids[0], &seen_message_ids[1],
            (MAX_SEEN_MESSAGE_IDS - 1) * sizeof(uint32_t));
    seen_message_ids[MAX_SEEN_MESSAGE_IDS - 1] = message_id;
    xSemaphoreGive(state_mutex);
}

int mesh_now_add_pending(const uint8_t *dest_mac, const uint8_t *remote_dest,
                         const uint8_t *wire, size_t wire_len,
                         uint32_t message_id, uint8_t flags, bool route_wait)
{
    // Allocate and initialize a pending slot under a single mutex hold so the
    // retransmit task never observes a partially-initialized entry.
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_PENDING_MESSAGES; ++i) {
        if (pending_messages[i].active) {
            continue;
        }
        pending_messages[i].message_id = message_id;
        pending_messages[i].flags = flags;
        memcpy(pending_messages[i].wire_buf, wire, wire_len);
        pending_messages[i].wire_len = wire_len;
        pending_messages[i].route_wait = route_wait;
        if (dest_mac != NULL) {
            memcpy(pending_messages[i].dest_mac, dest_mac, ESP_NOW_ETH_ALEN);
        }
        if (remote_dest != NULL) {
            memcpy(pending_messages[i].remote_dest, remote_dest,
                   ESP_NOW_ETH_ALEN);
        }
        pending_messages[i].retries = 0;
        pending_messages[i].last_send_time_ms = esp_timer_get_time() / 1000;
        pending_messages[i].active = true;
        xSemaphoreGive(state_mutex);
        return i;
    }
    xSemaphoreGive(state_mutex);
    return -1;
}

int mesh_now_find_pending(uint32_t message_id)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_PENDING_MESSAGES; ++i) {
        if (pending_messages[i].active &&
            pending_messages[i].message_id == message_id) {
            xSemaphoreGive(state_mutex);
            return i;
        }
    }
    xSemaphoreGive(state_mutex);
    return -1;
}

void mesh_now_release_pending(int index)
{
    if (index >= 0 && index < MAX_PENDING_MESSAGES) {
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        pending_messages[index].active = false;
        xSemaphoreGive(state_mutex);
    }
}

void mesh_now_set_receive_callback(mesh_now_receive_callback_t callback)
{
    receive_callback = callback;
}

void mesh_now_set_route_failure_callback(mesh_now_route_failure_callback_t cb)
{
    route_failure_callback = cb;
}

// An RREP has established a route to target: any DM buffered while the route
// was being discovered is delivered to the route's next hop right away.
void mesh_now_flush_route_wait(const uint8_t *target)
{
    mesh_route_t rt;
    if (!mesh_now_get_route(target, &rt) || !rt.active) {
        return;
    }

    // Collect the buffered message ids, then send each one outside the mutex.
    uint32_t flush_ids[MAX_PENDING_MESSAGES];
    int flush_count = 0;

    int64_t now_ms = esp_timer_get_time() / 1000;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    for (int i = 0;
         i < MAX_PENDING_MESSAGES && flush_count < MAX_PENDING_MESSAGES; ++i) {
        pending_message_t *pending = &pending_messages[i];
        if (!pending->active || !pending->route_wait ||
            memcmp(pending->remote_dest, target, ESP_NOW_ETH_ALEN) != 0) {
            continue;
        }
        memcpy(pending->dest_mac, rt.next_hop, ESP_NOW_ETH_ALEN);
        pending->route_wait = false;
        pending->retries = 0;
        pending->last_send_time_ms = now_ms;
        flush_ids[flush_count++] = pending->message_id;
    }
    xSemaphoreGive(state_mutex);

    for (int i = 0; i < flush_count; ++i) {
        uint8_t dest[ESP_NOW_ETH_ALEN];
        uint8_t wire[WIRE_BUF_SIZE];
        size_t wire_len = 0;
        uint32_t msg_id = flush_ids[i];
        int idx = -1;
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        for (int p = 0; p < MAX_PENDING_MESSAGES; ++p) {
            if (pending_messages[p].active &&
                pending_messages[p].message_id == msg_id) {
                idx = p;
                break;
            }
        }
        if (idx >= 0) {
            memcpy(dest, pending_messages[idx].dest_mac, ESP_NOW_ETH_ALEN);
            wire_len = pending_messages[idx].wire_len;
            memcpy(wire, pending_messages[idx].wire_buf, wire_len);
        }
        xSemaphoreGive(state_mutex);
        if (idx < 0) {
            continue;
        }
        esp_err_t ret = esp_now_send(dest, wire, wire_len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Route-ready send failed for %u: %s", msg_id,
                     esp_err_to_name(ret));
        }
    }
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
    return peers;
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
    size_t to_copy = peer_count < (int)max_out ? peer_count : max_out;
    for (size_t i = 0; i < to_copy; i++) {
        out[i] = peers[i];
    }
    int count = (int)to_copy;
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

    if (state_mutex != NULL) {
        vSemaphoreDelete(state_mutex);
        state_mutex = NULL;
    }

    ESP_LOGI(TAG, "ESP-NOW mesh networking deinitialized successfully");
    return ESP_OK;
}
