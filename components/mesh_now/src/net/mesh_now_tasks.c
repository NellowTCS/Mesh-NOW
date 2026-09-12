#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#define TAG "MESH_NOW"

// Peer snapshot for the beacon zone announce: a named MAC pair, sorted below
// so the densest subset survives frame-size trimming.
typedef struct {
    uint8_t mac[ESP_NOW_ETH_ALEN];
    char name[MESH_NOW_NODE_NAME_MAX + 1];
} zone_candidate_t;

// Advertise as many one-hop peers as fit in an ESP-NOW frame.
static uint8_t fill_zone_announce(mesh_message_t *beacon)
{
    zone_candidate_t candidates[MAX_BEACON_NEIGHBORS];
    uint8_t candidate_count = 0;

    xSemaphoreTake(state_mutex, portMAX_DELAY);
    for (int i = 0; i < peer_count && candidate_count < MAX_BEACON_NEIGHBORS;
         i++) {
        if (!peers[i].active) {
            continue;
        }
        memcpy(candidates[candidate_count].mac, peers[i].peer_addr,
               ESP_NOW_ETH_ALEN);
        strncpy(candidates[candidate_count].name, peers[i].node_name,
                MESH_NOW_NODE_NAME_MAX);
        candidates[candidate_count].name[MESH_NOW_NODE_NAME_MAX] = '\0';
        candidate_count++;
    }
    xSemaphoreGive(state_mutex);

    if (candidate_count == 0) {
        return 0;
    }

    // Insertion sort by name length, shortest first.
    for (uint8_t i = 1; i < candidate_count; i++) {
        zone_candidate_t key = candidates[i];
        size_t key_len = strlen(key.name);
        int j = (int)i - 1;
        while (j >= 0 && strlen(candidates[j].name) > key_len) {
            candidates[j + 1] = candidates[j];
            j--;
        }
        candidates[j + 1] = key;
    }

    // Trim until the frame fits. The encoder also folds in the local name and
    // beacon content, so the measured length is exact.
    uint8_t wire[WIRE_BUF_SIZE];
    for (uint8_t n = candidate_count; n > 0; n--) {
        beacon->neighbor_count = n;
        for (uint8_t i = 0; i < n; i++) {
            memcpy(beacon->neighbor_macs[i], candidates[i].mac,
                   ESP_NOW_ETH_ALEN);
            memcpy(beacon->neighbor_names[i], candidates[i].name,
                   MESH_NOW_NODE_NAME_MAX + 1);
        }
        size_t len = mesh_now_encode(beacon, wire, sizeof(wire));
        if (len != 0 && len <= ESP_NOW_MAX_DATA_LEN - MESH_NOW_HEADER_LEN) {
            return n;
        }
    }

    beacon->neighbor_count = 0;
    return 0;
}

static void build_beacon(mesh_message_t *beacon)
{
    memset(beacon, 0, sizeof(mesh_message_t));
    beacon->type = MSG_TYPE_BEACON;
    beacon->message_id = mesh_now_generate_message_id();
    beacon->hop_limit = 1;
    beacon->hop_count = 0;
    esp_read_mac(beacon->sender_mac, ESP_MAC_WIFI_STA);
    beacon->timestamp = mesh_now_get_network_time_ms();
    strncpy(beacon->message, "MESH-NOW-BEACON", MAX_MESH_MESSAGE_LEN - 1);

    // Zone announce: advertise as many one-hop peers as fit so two-hop nodes
    // learn routes and names without RREQ latency.
    fill_zone_announce(beacon);
}

static void beacon_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Beacon task started, broadcasting every %d ms",
             BEACON_INTERVAL_MS);

    int sweep_counter = 0;

    while (1) {
        mesh_message_t beacon;
        build_beacon(&beacon);
        mesh_now_mark_message_seen(beacon.message_id);
        mesh_now_send_frame(&beacon, broadcast_mac, false);

        if (++sweep_counter >= 6) {
            sweep_counter = 0;
            int64_t now_us = esp_timer_get_time();
            mesh_now_expire_peers(now_us);
            mesh_now_expire_routes(now_us);
            mesh_now_expire_route_requests(now_us);
        }

        vTaskDelay(pdMS_TO_TICKS(BEACON_INTERVAL_MS));
    }
}

esp_err_t mesh_now_start_tasks(void)
{
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        beacon_task, "beacon_task", 8192, NULL, 5, &beacon_task_handle, 0);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create beacon task");
        return ESP_FAIL;
    }

    task_ret =
        xTaskCreatePinnedToCore(mesh_now_retransmit_task_fn, "retransmit_task",
                                8192, NULL, 5, &retransmit_task_handle, 0);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create retransmit task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t mesh_now_announce_name(void)
{
    // Immediate beacon so peers see the rename without waiting for the next
    // periodic interval.
    mesh_message_t beacon;
    build_beacon(&beacon);
    mesh_now_mark_message_seen(beacon.message_id);
    return mesh_now_send_frame(&beacon, broadcast_mac, false);
}