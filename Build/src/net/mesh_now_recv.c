#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <string.h>

#define TAG "MESH_NOW"

// Forward a directed frame one hop toward target, flooding if no route is
// known so the frame still arrives.
static void forward_directed(const mesh_message_t *msg,
                             const uint8_t *target_mac)
{
    if (!mesh_now_can_relay(msg)) {
        return;
    }

    mesh_route_t rt;
    memset(&rt, 0, sizeof(rt));
    bool do_encrypt = !mesh_now_is_control_type(msg->type);

    const uint8_t *dest = broadcast_mac;
    if (mesh_now_peer_is_direct(target_mac)) {
        dest = target_mac;
    } else if (mesh_now_get_route(target_mac, &rt) && rt.active) {
        dest = rt.next_hop;
    } else if (mesh_now_route_request_attempts(target_mac) < 0) {
        // No route: flood this copy and start local repair in the background.
        mesh_now_request_route(target_mac);
    }

    esp_err_t ret = mesh_now_relay_unicast(msg, dest, do_encrypt);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Directed forward failed: %s", esp_err_to_name(ret));
    }
}

void mesh_now_handle_message(const mesh_message_t *mesh_msg)
{
    if (receive_callback) {
        receive_callback(mesh_msg);
    } else {
        message_t msg;
        memset(&msg, 0, sizeof(msg));
        strncpy(msg.message, mesh_msg->message, sizeof(msg.message) - 1);
        memcpy(msg.sender_mac, mesh_msg->sender_mac, sizeof(msg.sender_mac));
        memcpy(msg.target_mac, mesh_msg->target_mac, sizeof(msg.target_mac));
        msg.timestamp = mesh_msg->timestamp;
        msg.type = mesh_msg->type;
        msg.group_id = mesh_msg->group_id;
        message_queue_send(&msg);
    }
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void esp_now_send_cb(const esp_now_send_info_t *send_info,
                            esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "Send failed to %02x:%02x:%02x:%02x:%02x:%02x",
                 send_info->des_addr[0], send_info->des_addr[1],
                 send_info->des_addr[2], send_info->des_addr[3],
                 send_info->des_addr[4], send_info->des_addr[5]);
    }
}
#else
static void esp_now_send_cb(const uint8_t *mac_addr,
                            esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "Send failed to %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
                 mac_addr[4], mac_addr[5]);
    }
}
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void esp_now_recv_cb(const esp_now_recv_info_t *recv_info,
                            const uint8_t *data, int len)
{
    const uint8_t *src_addr = recv_info->src_addr;
#else
static void esp_now_recv_cb(const uint8_t *mac_addr, const uint8_t *data,
                            int len)
{
    const uint8_t *src_addr = mac_addr;
#endif

    mesh_message_t mesh_msg;
    if (!mesh_now_decode_wire(data, len, &mesh_msg)) {
        ESP_LOGW(TAG,
                 "Failed to decode wire message from "
                 "%02x:%02x:%02x:%02x:%02x:%02x (len=%d)",
                 src_addr[0], src_addr[1], src_addr[2], src_addr[3],
                 src_addr[4], src_addr[5], len);
        return;
    }

    ESP_LOGD(TAG,
             "Received message from %02x:%02x:%02x:%02x:%02x:%02x, "
             "type: %d, id: %u",
             src_addr[0], src_addr[1], src_addr[2], src_addr[3], src_addr[4],
             src_addr[5], mesh_msg.type, mesh_msg.message_id);

    uint8_t my_mac[ESP_NOW_ETH_ALEN];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    if (memcmp(mesh_msg.sender_mac, my_mac, ESP_NOW_ETH_ALEN) == 0) {
        return;
    }

    bool addressed_to_me =
        memcmp(mesh_msg.target_mac, my_mac, ESP_NOW_ETH_ALEN) == 0;
    bool is_directed =
        mesh_msg.type == MSG_TYPE_DIRECT || mesh_msg.type == MSG_TYPE_TYPING;

    // Flood data (chat/group/presence) dedups at every node so a broadcast
    // lands once; directed frames dedup only at the final destination.
    if (!mesh_now_is_control_type(mesh_msg.type)) {
        bool dedup = is_directed ? addressed_to_me : true;
        if (dedup && mesh_now_is_message_seen(mesh_msg.message_id)) {
            // Duplicate DIRECT addressed to us: the original arrived but our
            // ACK was lost. Re-ACK so the sender stops retrying and does not
            // drop it.
            if (mesh_msg.type == MSG_TYPE_DIRECT && addressed_to_me) {
                mesh_now_send_ack(&mesh_msg, src_addr);
            }
            ESP_LOGD(TAG, "Duplicate message %u ignored", mesh_msg.message_id);
            return;
        }
        if (dedup) {
            mesh_now_mark_message_seen(mesh_msg.message_id);
        }
    }

    if (mesh_msg.type == MSG_TYPE_BEACON) {
        ESP_LOGD(TAG,
                 "Received beacon from %02x:%02x:%02x:%02x:%02x:%02x%s%s%s",
                 mesh_msg.sender_mac[0], mesh_msg.sender_mac[1],
                 mesh_msg.sender_mac[2], mesh_msg.sender_mac[3],
                 mesh_msg.sender_mac[4], mesh_msg.sender_mac[5],
                 mesh_msg.node_name[0] ? " (" : "",
                 mesh_msg.node_name[0] ? mesh_msg.node_name : "",
                 mesh_msg.node_name[0] ? ")" : "");
        mesh_now_add_peer(mesh_msg.sender_mac);
        mesh_now_sync_time(mesh_msg.timestamp);

        if (mesh_msg.node_name[0] != '\0') {
            xSemaphoreTake(state_mutex, portMAX_DELAY);
            for (int i = 0; i < peer_count; i++) {
                if (peers[i].active &&
                    memcmp(peers[i].peer_addr, mesh_msg.sender_mac,
                           ESP_NOW_ETH_ALEN) == 0) {
                    strncpy(peers[i].node_name, mesh_msg.node_name,
                            MESH_NOW_NODE_NAME_MAX);
                    peers[i].node_name[MESH_NOW_NODE_NAME_MAX] = '\0';
                    break;
                }
            }
            xSemaphoreGive(state_mutex);
        }

        // Zone routes: each advertised neighbor is a 2-hop virtual peer via the
        // beacon's sender.
        for (uint8_t i = 0; i < mesh_msg.neighbor_count; i++) {
            mesh_now_add_virtual_peer(mesh_msg.neighbor_macs[i],
                                      mesh_msg.sender_mac,
                                      mesh_msg.neighbor_names[i], 2);
        }
    } else if (mesh_msg.type == MSG_TYPE_ACK) {
        if (!addressed_to_me) {
            forward_directed(&mesh_msg, mesh_msg.target_mac);
            return;
        }

        int pending_index = mesh_now_find_pending(mesh_msg.reply_to);
        if (pending_index >= 0) {
            mesh_now_release_pending(pending_index);
            ESP_LOGD(TAG, "Received ACK for message %u", mesh_msg.reply_to);
        }
    } else if (mesh_msg.type == MSG_TYPE_ROUTE_REQUEST) {
        mesh_now_handle_rreq(&mesh_msg, src_addr);
    } else if (mesh_msg.type == MSG_TYPE_ROUTE_REPLY) {
        mesh_now_handle_rrep(&mesh_msg, src_addr);
    } else if (mesh_msg.type == MSG_TYPE_ROUTE_ERROR) {
        mesh_now_handle_rerr(&mesh_msg);
    } else if (mesh_msg.type == MSG_TYPE_CHAT) {
        mesh_now_handle_message(&mesh_msg);

        if (mesh_now_can_relay(&mesh_msg)) {
            mesh_now_relay_broadcast(&mesh_msg, true);
        }
    } else if (mesh_msg.type == MSG_TYPE_DIRECT) {
        if (!addressed_to_me) {
            forward_directed(&mesh_msg, mesh_msg.target_mac);
            return;
        }

        mesh_now_send_ack(&mesh_msg, src_addr);
        mesh_now_handle_message(&mesh_msg);
    } else if (mesh_msg.type == MSG_TYPE_GROUP) {
        if (local_group_id != 0 && mesh_msg.group_id == local_group_id) {
            mesh_now_handle_message(&mesh_msg);
        }

        if (mesh_now_can_relay(&mesh_msg)) {
            mesh_now_relay_broadcast(&mesh_msg, true);
        }
    } else if (mesh_msg.type == MSG_TYPE_PRESENCE) {
        mesh_now_handle_message(&mesh_msg);

        if (mesh_now_can_relay(&mesh_msg)) {
            mesh_now_relay_broadcast(&mesh_msg, true);
        }
    } else if (mesh_msg.type == MSG_TYPE_TYPING) {
        if (addressed_to_me) {
            mesh_now_handle_message(&mesh_msg);
        } else {
            forward_directed(&mesh_msg, mesh_msg.target_mac);
        }
    }
}

void mesh_now_register_callbacks(void)
{
    esp_now_register_send_cb(esp_now_send_cb);
    esp_now_register_recv_cb(esp_now_recv_cb);
}
