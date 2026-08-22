#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#define TAG "MESH_NOW"

static esp_err_t mesh_now_send_wire(const uint8_t *dest_mac,
                                     const uint8_t *wire, size_t wire_len,
                                     bool queue_for_retransmit,
                                     uint32_t message_id)
{
    if (queue_for_retransmit) {
        int index = mesh_now_allocate_pending();
        if (index < 0) {
            ESP_LOGW(TAG, "No pending slots available");
            return ESP_ERR_NO_MEM;
        }
        pending_messages[index].active = true;
        pending_messages[index].message_id = message_id;
        memcpy(pending_messages[index].wire_buf, wire, wire_len);
        pending_messages[index].wire_len = wire_len;
        memcpy(pending_messages[index].dest_mac, dest_mac, ESP_NOW_ETH_ALEN);
        pending_messages[index].retries = 0;
        pending_messages[index].last_send_time_ms = esp_timer_get_time() / 1000;
    }

    esp_err_t ret = esp_now_send(dest_mac, wire, wire_len);
    if (ret != ESP_OK && queue_for_retransmit) {
        int idx = mesh_now_find_pending(message_id);
        if (idx >= 0) {
            pending_messages[idx].active = false;
        }
    }
    return ret;
}

static esp_err_t mesh_now_send_message_packet(mesh_message_t *msg,
                                               bool queue_for_retransmit)
{
    msg->message_id = mesh_now_generate_message_id();
    msg->hop_count = DEFAULT_ROUTE_TTL;
    esp_read_mac(msg->sender_mac, ESP_MAC_WIFI_STA);
    msg->timestamp = esp_timer_get_time() / 1000;

    mesh_now_mark_message_seen(msg->message_id);

    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    esp_err_t err = mesh_now_prepare_wire(msg, wire, &wire_len, true);
    if (err != ESP_OK) {
        return err;
    }

    esp_err_t ret = mesh_now_send_wire(broadcast_mac, wire, wire_len,
                                        queue_for_retransmit, msg->message_id);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sent message type %d id %u (%u bytes)",
                 msg->type, msg->message_id, (unsigned)wire_len);
    }
    return ret;
}

void mesh_now_route_message(mesh_message_t *msg)
{
    if (msg->hop_count == 0) {
        return;
    }

    mesh_message_t forward = *msg;
    forward.hop_count--;

    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    esp_err_t err = mesh_now_prepare_wire(&forward, wire, &wire_len, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to encode routed message: %s", esp_err_to_name(err));
        return;
    }

    esp_err_t ret = esp_now_send(broadcast_mac, wire, wire_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to route message %u: %s",
                 forward.message_id, esp_err_to_name(ret));
    }
}

void mesh_now_send_ack(const mesh_message_t *received_msg)
{
    mesh_message_t ack_msg;
    memset(&ack_msg, 0, sizeof(mesh_message_t));
    ack_msg.type = MSG_TYPE_ACK;
    ack_msg.message_id = received_msg->message_id;
    ack_msg.hop_count = DEFAULT_ROUTE_TTL;
    esp_read_mac(ack_msg.sender_mac, ESP_MAC_WIFI_STA);
    memcpy(ack_msg.target_mac, received_msg->sender_mac, ESP_NOW_ETH_ALEN);

    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    esp_err_t err = mesh_now_prepare_wire(&ack_msg, wire, &wire_len, false);
    if (err != ESP_OK) {
        return;
    }

    esp_err_t ret = esp_now_send(broadcast_mac, wire, wire_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send ACK for message %u: %s",
                 received_msg->message_id, esp_err_to_name(ret));
    }
}

static void retransmit_task(void *pvParameters)
{
    while (1) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        for (int i = 0; i < MAX_PENDING_MESSAGES; ++i) {
            pending_message_t *pending = &pending_messages[i];
            if (!pending->active) {
                continue;
            }

            if (now_ms - pending->last_send_time_ms < RETRANSMIT_TIMEOUT_MS) {
                continue;
            }

            if (pending->retries >= 3) {
                ESP_LOGW(TAG, "Dropping pending message after %d retries",
                         pending->retries);
                pending->active = false;
                continue;
            }

            pending->retries++;
            pending->last_send_time_ms = now_ms;
            esp_err_t ret = esp_now_send(pending->dest_mac,
                                          pending->wire_buf,
                                          pending->wire_len);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Retransmitted pending message (retry %d)",
                         pending->retries);
            } else {
                ESP_LOGW(TAG, "Retransmit failed: %s", esp_err_to_name(ret));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void beacon_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Beacon task started, broadcasting every %d ms",
             BEACON_INTERVAL_MS);

    int sweep_counter = 0;

    while (1) {
        mesh_message_t beacon;
        memset(&beacon, 0, sizeof(mesh_message_t));
        beacon.type = MSG_TYPE_BEACON;
        beacon.message_id = mesh_now_generate_message_id();
        beacon.hop_count = 1;
        esp_read_mac(beacon.sender_mac, ESP_MAC_WIFI_STA);
        beacon.timestamp = esp_timer_get_time() / 1000;
        strncpy(beacon.message, "MESH-NOW-BEACON", MAX_MESH_MESSAGE_LEN - 1);

        mesh_now_mark_message_seen(beacon.message_id);

        uint8_t wire[WIRE_BUF_SIZE];
        size_t wire_len = 0;
        esp_err_t err = mesh_now_prepare_wire(&beacon, wire, &wire_len, false);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to encode beacon: %s", esp_err_to_name(err));
        } else {
            esp_err_t ret = esp_now_send(broadcast_mac, wire, wire_len);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Beacon broadcast failed: %s",
                         esp_err_to_name(ret));
            }
        }

        if (++sweep_counter >= 6) {
            sweep_counter = 0;
            int64_t now = esp_timer_get_time();
            for (int i = 0; i < peer_count; i++) {
                if (peers[i].active &&
                    (now - peers[i].last_seen) > PEER_EXPIRY_US) {
                    ESP_LOGI(TAG, "Peer expired: %02x:%02x:%02x:%02x:%02x:%02x",
                             peers[i].peer_addr[0], peers[i].peer_addr[1],
                             peers[i].peer_addr[2], peers[i].peer_addr[3],
                             peers[i].peer_addr[4], peers[i].peer_addr[5]);
                    peers[i].active = false;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BEACON_INTERVAL_MS));
    }
}

esp_err_t mesh_now_start_tasks(void)
{
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        beacon_task, "beacon_task", 4096, NULL, 5,
        &beacon_task_handle, 0);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create beacon task");
        return ESP_FAIL;
    }

    task_ret = xTaskCreatePinnedToCore(
        retransmit_task, "retransmit_task", 4096, NULL, 5,
        &retransmit_task_handle, 0);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create retransmit task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t mesh_now_send_broadcast(const char *message)
{
    mesh_message_t msg;
    memset(&msg, 0, sizeof(mesh_message_t));
    msg.type = MSG_TYPE_CHAT;
    strncpy(msg.message, message, sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    return mesh_now_send_message_packet(&msg, false);
}

esp_err_t mesh_now_send_message(const char *message)
{
    return mesh_now_send_broadcast(message);
}

esp_err_t mesh_now_send_direct(const uint8_t *target_mac, const char *message)
{
    if (target_mac == NULL || message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    mesh_message_t msg;
    memset(&msg, 0, sizeof(mesh_message_t));
    msg.type = MSG_TYPE_DIRECT;
    msg.flags = MSG_FLAG_REQUIRES_ACK;
    memcpy(msg.target_mac, target_mac, ESP_NOW_ETH_ALEN);
    strncpy(msg.message, message, sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    return mesh_now_send_message_packet(&msg, true);
}

esp_err_t mesh_now_send_group(uint8_t group_id, const char *message)
{
    mesh_message_t msg;
    memset(&msg, 0, sizeof(mesh_message_t));
    msg.type = MSG_TYPE_GROUP;
    msg.group_id = group_id;
    strncpy(msg.message, message, sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    return mesh_now_send_message_packet(&msg, false);
}

esp_err_t mesh_now_send_presence(const char *status)
{
    mesh_message_t msg;
    memset(&msg, 0, sizeof(mesh_message_t));
    msg.type = MSG_TYPE_PRESENCE;
    strncpy(msg.message, status, sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    return mesh_now_send_message_packet(&msg, false);
}

esp_err_t mesh_now_send_typing(const uint8_t *target_mac, bool typing)
{
    if (target_mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    mesh_message_t msg;
    memset(&msg, 0, sizeof(mesh_message_t));
    msg.type = MSG_TYPE_TYPING;
    memcpy(msg.target_mac, target_mac, ESP_NOW_ETH_ALEN);
    strncpy(msg.message, typing ? "typing" : "stopped", sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    return mesh_now_send_message_packet(&msg, false);
}
