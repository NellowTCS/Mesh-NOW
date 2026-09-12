#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#define TAG "MESH_NOW"

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

    // Zone announce: advertise up to MAX_BEACON_NEIGHBORS one-hop peers so
    // two-hop nodes learn routes and names without RREQ latency.
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    for (int i = 0;
         i < peer_count && beacon->neighbor_count < MAX_BEACON_NEIGHBORS; i++) {
        if (!peers[i].active) {
            continue;
        }
        memcpy(beacon->neighbor_macs[beacon->neighbor_count],
               peers[i].peer_addr, ESP_NOW_ETH_ALEN);
        strncpy(beacon->neighbor_names[beacon->neighbor_count],
                peers[i].node_name, MESH_NOW_NODE_NAME_MAX);
        beacon->neighbor_names[beacon->neighbor_count][MESH_NOW_NODE_NAME_MAX] =
            '\0';
        beacon->neighbor_count++;
    }
    xSemaphoreGive(state_mutex);
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