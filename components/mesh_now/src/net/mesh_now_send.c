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
        if (mesh_now_add_pending(dest_mac, NULL, wire, wire_len, message_id,
                                 flags, false) < 0) {
            ESP_LOGW(TAG, "No pending slots available");
            return ESP_ERR_NO_MEM;
        }
        return esp_now_send(dest_mac, wire, wire_len);
    }

    return esp_now_send(dest_mac, wire, wire_len);
}

// Fill the origin fields shared by every outgoing frame, then mark it seen so
// a looped-back copy of our own broadcast is ignored.
static void finalize_packet(mesh_message_t *msg)
{
    msg->message_id = mesh_now_generate_message_id();
    msg->hop_limit = DEFAULT_ROUTE_TTL;
    msg->hop_count = 0;
    esp_read_mac(msg->sender_mac, ESP_MAC_WIFI_STA);
    msg->timestamp = mesh_now_get_network_time_ms();
    mesh_now_mark_message_seen(msg->message_id);
}

// Send a targeted frame for dest, choosing the physical path: one-hop
// neighbor unicast, a cached virtual-peer route, or buffered while a route
// request runs.
static esp_err_t mesh_now_send_directed(const uint8_t *target_mac,
                                        const char *message, uint8_t type,
                                        uint8_t flags)
{
    mesh_message_t msg;
    memset(&msg, 0, sizeof(mesh_message_t));
    msg.type = type;
    msg.flags = flags;
    memcpy(msg.target_mac, target_mac, ESP_NOW_ETH_ALEN);
    strncpy(msg.message, message, sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    finalize_packet(&msg);

    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    esp_err_t err = mesh_now_prepare_wire(&msg, wire, &wire_len, true);
    if (err != ESP_OK) {
        return err;
    }

    if (mesh_now_peer_is_direct(target_mac)) {
        return mesh_now_send_wire(target_mac, wire, wire_len, true,
                                  msg.message_id, msg.flags);
    }

    mesh_route_t rt;
    memset(&rt, 0, sizeof(rt));
    if (mesh_now_get_route(target_mac, &rt) && rt.active) {
        // Two-hop DM: unicast a single copy to the next hop; it relays
        // toward the target.
        ESP_LOGD(TAG,
                 "Routed DM %u to %02x:%02x:%02x:%02x:%02x:%02x via next hop",
                 msg.message_id, target_mac[0], target_mac[1], target_mac[2],
                 target_mac[3], target_mac[4], target_mac[5]);
        if (mesh_now_add_pending(rt.next_hop, target_mac, wire, wire_len,
                                 msg.message_id, msg.flags, false) < 0) {
            return ESP_ERR_NO_MEM;
        }
        return esp_now_send(rt.next_hop, wire, wire_len);
    }

    // No path yet: buffer the DM and ask the mesh for a route.
    if (mesh_now_add_pending(NULL, target_mac, wire, wire_len, msg.message_id,
                             msg.flags, true) < 0) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGD(
        TAG,
        "Buffering DM %u, requesting route to %02x:%02x:%02x:%02x:%02x:%02x",
        msg.message_id, target_mac[0], target_mac[1], target_mac[2],
        target_mac[3], target_mac[4], target_mac[5]);
    return mesh_now_request_route(target_mac);
}

static esp_err_t mesh_now_send_message_packet(mesh_message_t *msg,
                                              bool queue_for_retransmit)
{
    finalize_packet(msg);

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
    // hop_count is cumulative: drop once forwarding would reach hop_limit.
    if (msg->hop_count + 1 >= msg->hop_limit) {
        return;
    }

    mesh_message_t forward = *msg;
    forward.hop_count++;

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
    ack_msg.hop_limit = DEFAULT_ROUTE_TTL;
    ack_msg.hop_count = 0;
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

// Progress the retransmit state machine for pending[i].
static void retransmit_task(void *pvParameters)
{
    while (1) {
        int64_t now_ms = esp_timer_get_time() / 1000;

        // route-wait pendings drive RREQ retries.
        for (int i = 0; i < MAX_PENDING_MESSAGES; ++i) {
            bool active = false;
            bool route_wait = false;
            uint8_t remote_dest[ESP_NOW_ETH_ALEN] = {0};
            int64_t last_send_ms = 0;
            xSemaphoreTake(state_mutex, portMAX_DELAY);
            active = pending_messages[i].active;
            route_wait = pending_messages[i].route_wait;
            if (active && route_wait) {
                memcpy(remote_dest, pending_messages[i].remote_dest,
                       ESP_NOW_ETH_ALEN);
                last_send_ms = pending_messages[i].last_send_time_ms;
            }
            xSemaphoreGive(state_mutex);

            if (!active || !route_wait) {
                continue;
            }
            if (now_ms - last_send_ms <
                (int64_t)(ROUTE_REQ_TIMEOUT_US / 1000)) {
                continue;
            }

            int attempts = mesh_now_route_request_attempts(remote_dest);
            if (attempts <= MAX_ROUTE_REQ_RETRIES) {
                esp_err_t rc = mesh_now_request_route(remote_dest);
                if (rc != ESP_OK) {
                    ESP_LOGW(TAG, "Route request failed: %s",
                             esp_err_to_name(rc));
                }
                xSemaphoreTake(state_mutex, portMAX_DELAY);
                pending_messages[i].last_send_time_ms = now_ms;
                xSemaphoreGive(state_mutex);
            } else {
                ESP_LOGW(TAG,
                         "No route found for "
                         "%02x:%02x:%02x:%02x:%02x:%02x after %d "
                         "requests, dropping message",
                         remote_dest[0], remote_dest[1], remote_dest[2],
                         remote_dest[3], remote_dest[4], remote_dest[5],
                         attempts);
                xSemaphoreTake(state_mutex, portMAX_DELAY);
                pending_messages[i].active = false;
                xSemaphoreGive(state_mutex);
                if (route_failure_callback) {
                    route_failure_callback(remote_dest);
                }
            }
        }

        // normal ACK-based retransmission.
        for (int i = 0; i < MAX_PENDING_MESSAGES; ++i) {
            bool do_send = false;
            bool do_drop = false;
            uint8_t dest_mac[ESP_NOW_ETH_ALEN];
            uint32_t message_id = 0;
            xSemaphoreTake(state_mutex, portMAX_DELAY);

            pending_message_t *pending = &pending_messages[i];
            if (pending->active && !pending->route_wait) {
                if (pending->flags & MSG_FLAG_REQUIRES_ACK) {
                    if (now_ms - pending->last_send_time_ms >=
                        RETRANSMIT_TIMEOUT_MS) {
                        if (pending->retries >= MAX_RETRIES) {
                            do_drop = true;
                            ESP_LOGW(TAG,
                                     "Dropping pending message after %d "
                                     "retries",
                                     pending->retries);
                        } else {
                            pending->retries++;
                            pending->last_send_time_ms = now_ms;
                            do_send = true;
                        }
                    }
                } else {
                    do_drop = true;
                }
            }
            if (do_send || do_drop) {
                memcpy(dest_mac, pending->dest_mac, ESP_NOW_ETH_ALEN);
                message_id = pending->message_id;
            }
            xSemaphoreGive(state_mutex);

            if (do_drop) {
                mesh_now_release_pending(i);
                continue;
            }
            if (!do_send) {
                continue;
            }

            int idx = mesh_now_find_pending(message_id);
            if (idx < 0) {
                continue;
            }
            size_t wire_len = 0;
            uint8_t wire[WIRE_BUF_SIZE];
            int retries = 0;
            xSemaphoreTake(state_mutex, portMAX_DELAY);
            wire_len = pending_messages[idx].wire_len;
            memcpy(wire, pending_messages[idx].wire_buf, wire_len);
            retries = pending_messages[idx].retries;
            xSemaphoreGive(state_mutex);

            esp_err_t ret = esp_now_send(dest_mac, wire, wire_len);
            if (ret == ESP_OK) {
                ESP_LOGD(TAG, "Retransmitted pending message (retry %d)",
                         retries);
            } else {
                ESP_LOGW(TAG, "Retransmit failed: %s", esp_err_to_name(ret));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
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
    return mesh_now_send_directed(target_mac, message, MSG_TYPE_DIRECT,
                                  MSG_FLAG_REQUIRES_ACK);
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
    return mesh_now_send_directed(target_mac, typing ? "typing" : "stopped",
                                  MSG_TYPE_TYPING, 0);
}
