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
                                    uint32_t message_id, uint8_t flags)
{
    if (queue_for_retransmit) {
        if (mesh_now_add_pending(dest_mac, wire, wire_len, message_id, flags) <
            0) {
            ESP_LOGW(TAG, "No pending slots available");
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t ret = esp_now_send(dest_mac, wire, wire_len);
    if (ret != ESP_OK && queue_for_retransmit) {
        int idx = mesh_now_find_pending(message_id);
        if (idx >= 0) {
            mesh_now_release_pending(idx);
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
    msg->timestamp = mesh_now_get_network_time_ms();

    mesh_now_mark_message_seen(msg->message_id);

    // Targeted traffic (DMs, typing) goes unicast: unicast runs at a much
    // higher WiFi rate than broadcast and gets MAC-layer retries, so it
    // costs a fraction of the airtime.
    static const uint8_t zero_mac[ESP_NOW_ETH_ALEN] = {0};
    const uint8_t *dest =
        memcmp(msg->target_mac, zero_mac, ESP_NOW_ETH_ALEN) != 0
            ? msg->target_mac
            : broadcast_mac;

    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    esp_err_t err = mesh_now_prepare_wire(msg, wire, &wire_len, true);
    if (err != ESP_OK) {
        return err;
    }

    esp_err_t ret =
        mesh_now_send_wire(dest, wire, wire_len, queue_for_retransmit,
                           msg->message_id, msg->flags);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Sent message type %d id %u (%u bytes)", msg->type,
                 msg->message_id, (unsigned)wire_len);
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
        ESP_LOGW(TAG, "Failed to encode routed message: %s",
                 esp_err_to_name(err));
        return;
    }

    esp_err_t ret = esp_now_send(broadcast_mac, wire, wire_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to route message %u: %s", forward.message_id,
                 esp_err_to_name(ret));
    }
}

void mesh_now_send_ack(const mesh_message_t *received_msg)
{
    mesh_message_t ack_msg;
    memset(&ack_msg, 0, sizeof(mesh_message_t));
    ack_msg.type = MSG_TYPE_ACK;
    // Give the ACK its own fresh message id so it can participate in
    // seen-message dedup
    ack_msg.message_id = mesh_now_generate_message_id();
    ack_msg.reply_to = received_msg->message_id;
    ack_msg.hop_count = DEFAULT_ROUTE_TTL;
    esp_read_mac(ack_msg.sender_mac, ESP_MAC_WIFI_STA);
    memcpy(ack_msg.target_mac, received_msg->sender_mac, ESP_NOW_ETH_ALEN);

    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    esp_err_t err = mesh_now_prepare_wire(&ack_msg, wire, &wire_len, false);
    if (err != ESP_OK) {
        return;
    }

    // Unicast back to the originator: broadcast ACKs waste airtime and get
    // lost under load, which causes false retransmit drops on the sender.
    esp_err_t ret = esp_now_send(received_msg->sender_mac, wire, wire_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send ACK for message %u: %s",
                 received_msg->message_id, esp_err_to_name(ret));
    }
}

static void retransmit_task(void *pvParameters)
{
    while (1) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_PENDING_MESSAGES; ++i) {
            pending_message_t *pending = &pending_messages[i];
            if (!pending->active) {
                continue;
            }

            if (!(pending->flags & MSG_FLAG_REQUIRES_ACK)) {
                pending->active = false;
                continue;
            }

            if (now_ms - pending->last_send_time_ms < RETRANSMIT_TIMEOUT_MS) {
                continue;
            }

            if (pending->retries >= MAX_RETRIES) {
                ESP_LOGW(TAG, "Dropping pending message after %d retries",
                         pending->retries);
                pending->active = false;
                continue;
            }

            pending->retries++;
            pending->last_send_time_ms = now_ms;
            esp_err_t ret = esp_now_send(pending->dest_mac, pending->wire_buf,
                                         pending->wire_len);
            if (ret == ESP_OK) {
                ESP_LOGD(TAG, "Retransmitted pending message (retry %d)",
                         pending->retries);
            } else {
                ESP_LOGW(TAG, "Retransmit failed: %s", esp_err_to_name(ret));
            }
        }
        xSemaphoreGive(state_mutex);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void build_beacon(mesh_message_t *beacon)
{
    memset(beacon, 0, sizeof(mesh_message_t));
    beacon->type = MSG_TYPE_BEACON;
    beacon->message_id = mesh_now_generate_message_id();
    beacon->hop_count = 1;
    esp_read_mac(beacon->sender_mac, ESP_MAC_WIFI_STA);
    beacon->timestamp = mesh_now_get_network_time_ms();
    strncpy(beacon->message, "MESH-NOW-BEACON", MAX_MESH_MESSAGE_LEN - 1);
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
            mesh_now_expire_peers(esp_timer_get_time());
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

    task_ret = xTaskCreatePinnedToCore(retransmit_task, "retransmit_task", 8192,
                                       NULL, 5, &retransmit_task_handle, 0);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create retransmit task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t mesh_now_announce_name(void)
{
    // Send an immediate beacon so peers pick up a renamed node without
    // waiting for the next periodic beacon interval.
    mesh_message_t beacon;
    build_beacon(&beacon);

    mesh_now_mark_message_seen(beacon.message_id);

    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    esp_err_t err = mesh_now_prepare_wire(&beacon, wire, &wire_len, false);
    if (err != ESP_OK) {
        return err;
    }
    return esp_now_send(broadcast_mac, wire, wire_len);
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
    strncpy(msg.message, typing ? "typing" : "stopped",
            sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    return mesh_now_send_message_packet(&msg, false);
}
