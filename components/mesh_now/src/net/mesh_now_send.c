#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <esp_mac.h>
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
    }
    return esp_now_send(dest_mac, wire, wire_len);
}

// Stamp origin fields, then mark seen so our own looped-back broadcast is
// ignored.
static void finalize_packet(mesh_message_t *msg)
{
    msg->message_id = mesh_now_generate_message_id();
    msg->hop_limit = DEFAULT_ROUTE_TTL;
    msg->hop_count = 0;
    esp_read_mac(msg->sender_mac, ESP_MAC_WIFI_STA);
    msg->timestamp = mesh_now_get_network_time_ms();
    mesh_now_mark_message_seen(msg->message_id);
}

// Target frames use the shortest path available: direct unicast to a
// neighbor, one hop over a route, otherwise buffer while discovery runs.
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
        // Multi-hop DM: unicast one copy to the next hop, which relays it on.
        ESP_LOGD(TAG, "Routed DM %u to %s via next hop", msg.message_id,
                 mesh_now_mac_str(target_mac));
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
    ESP_LOGD(TAG, "Buffering DM %u, requesting route to %s", msg.message_id,
             mesh_now_mac_str(target_mac));
    return mesh_now_request_route(target_mac);
}

static esp_err_t mesh_now_send_message_packet(mesh_message_t *msg,
                                              bool queue_for_retransmit)
{
    finalize_packet(msg);

    // Targeted traffic unicasts: higher WiFi rate than broadcast plus MAC-layer
    // retries, so it costs a fraction of the airtime.
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

void mesh_now_send_ack(const mesh_message_t *received_msg,
                       const uint8_t *prev_hop)
{
    mesh_message_t ack_msg;
    memset(&ack_msg, 0, sizeof(mesh_message_t));
    ack_msg.type = MSG_TYPE_ACK;
    // Fresh id so the ACK participates in seen-message dedup.
    ack_msg.message_id = mesh_now_generate_message_id();
    ack_msg.reply_to = received_msg->message_id;
    ack_msg.hop_limit = DEFAULT_ROUTE_TTL;
    ack_msg.hop_count = 0;
    esp_read_mac(ack_msg.sender_mac, ESP_MAC_WIFI_STA);
    // Back to whoever sent the DM.
    memcpy(ack_msg.target_mac, received_msg->sender_mac, ESP_NOW_ETH_ALEN);

    // Hand back to the previous hop; it relays the rest of the way.
    if (mesh_now_send_frame(&ack_msg, prev_hop, false) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send ACK for message %u",
                 received_msg->message_id);
    }
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