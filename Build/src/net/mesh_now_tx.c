#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <stdio.h>
#include <string.h>

#define TAG "MESH_NOW"

// Format a MAC for a log line. A ring of buffers keeps up to four nested
// calls in one statement distinct.
const char *mesh_now_mac_str(const uint8_t *mac)
{
    static char buf[4][18];
    static uint8_t next;

    char *dst = buf[next++ & 3];
    snprintf(dst, sizeof(buf[0]), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0],
             mac[1], mac[2], mac[3], mac[4], mac[5]);
    return dst;
}

// hop_count is cumulative: a frame may be relayed only while forwarding keeps
// it within its TTL. Single source of truth for the boundary check.
bool mesh_now_can_relay(const mesh_message_t *msg)
{
    return msg->hop_count + 1 < msg->hop_limit;
}

// Encode and send one frame. The common tail of every outbound path; control
// frames pass do_encrypt=false, data frames true.
esp_err_t mesh_now_send_frame(const mesh_message_t *msg, const uint8_t *dest,
                              bool do_encrypt)
{
    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    esp_err_t err = mesh_now_prepare_wire(msg, wire, &wire_len, do_encrypt);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to encode frame type %d: %s", msg->type,
                 esp_err_to_name(err));
        return err;
    }

    if (wire_len > ESP_NOW_MAX_DATA_LEN) {
        ESP_LOGW(TAG, "Frame type %d too large (%u > %u bytes), dropping",
                 msg->type, (unsigned)wire_len, (unsigned)ESP_NOW_MAX_DATA_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = esp_now_send(dest, wire, wire_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send frame type %d: %s", msg->type,
                 esp_err_to_name(ret));
    }
    return ret;
}

// Relay a frame one hop toward next_hop, dropping it if the TTL is exhausted.
esp_err_t mesh_now_relay_unicast(const mesh_message_t *msg,
                                 const uint8_t *next_hop, bool do_encrypt)
{
    if (!mesh_now_can_relay(msg)) {
        return ESP_OK;
    }

    mesh_message_t forward = *msg;
    forward.hop_count++;
    return mesh_now_send_frame(&forward, next_hop, do_encrypt);
}

// Bounded-flood fallback used when no unicast path exists.
esp_err_t mesh_now_relay_broadcast(const mesh_message_t *msg, bool do_encrypt)
{
    return mesh_now_relay_unicast(msg, broadcast_mac, do_encrypt);
}