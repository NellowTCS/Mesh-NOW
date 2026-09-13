#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <mpack.h>
#include <string.h>

#define TAG "MESH_NOW"

bool mesh_now_is_control_type(uint8_t type)
{
    return type == MSG_TYPE_BEACON || type == MSG_TYPE_ACK ||
           type == MSG_TYPE_ROUTE_REQUEST || type == MSG_TYPE_ROUTE_REPLY ||
           type == MSG_TYPE_ROUTE_ERROR;
}

// Control frames that carry an endpoint name so both ends of a conversation
// learn names on first contact (beacons announce their own name periodically).
bool mesh_now_carries_node_name(uint8_t type)
{
    return type == MSG_TYPE_BEACON || type == MSG_TYPE_ROUTE_REQUEST ||
           type == MSG_TYPE_ROUTE_REPLY;
}

// Snapshot the local name under the state mutex.
static void copy_local_name(char *dst, size_t dst_size)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    strncpy(dst, local_node_name, dst_size - 1);
    dst[dst_size - 1] = '\0';
    xSemaphoreGive(state_mutex);
}

size_t mesh_now_encode(const mesh_message_t *msg, uint8_t *out, size_t out_size)
{
    mpack_writer_t writer;
    mpack_writer_init(&writer, (char *)out, out_size);

    char name[MESH_NOW_NODE_NAME_MAX + 1];
    copy_local_name(name, sizeof(name));

    bool has_name = (name[0] != '\0') && mesh_now_carries_node_name(msg->type);
    mpack_build_map(&writer);

    size_t content_len = strnlen(msg->message, MAX_MESH_MESSAGE_LEN);
    mpack_write_cstr(&writer, "content");
    mpack_write_str(&writer, msg->message, content_len);

    if (has_name) {
        mpack_write_cstr(&writer, "node_name");
        mpack_write_str(&writer, name, strlen(name));
    }
    if (msg->type == MSG_TYPE_BEACON && msg->neighbor_count > 0) {
        mpack_write_cstr(&writer, "neighbor_macs");
        mpack_start_array(&writer, msg->neighbor_count);
        for (uint8_t i = 0; i < msg->neighbor_count; i++) {
            mpack_write_bin(&writer, (const char *)msg->neighbor_macs[i],
                            ESP_NOW_ETH_ALEN);
        }
        mpack_finish_array(&writer);

        mpack_write_cstr(&writer, "neighbor_names");
        mpack_start_array(&writer, msg->neighbor_count);
        for (uint8_t i = 0; i < msg->neighbor_count; i++) {
            size_t nn_len =
                strnlen(msg->neighbor_names[i], MESH_NOW_NODE_NAME_MAX);
            mpack_write_str(&writer, msg->neighbor_names[i], nn_len);
        }
        mpack_finish_array(&writer);
    }
    mpack_complete_map(&writer);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        return 0;
    }
    return mpack_writer_buffer_used(&writer);
}

bool mesh_now_decode(const uint8_t *data, size_t len, mesh_message_t *msg)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, (const char *)data, len);
    mpack_tree_parse(&tree);

    if (mpack_tree_error(&tree) != mpack_ok) {
        mpack_tree_destroy(&tree);
        return false;
    }

    mpack_node_t root = mpack_tree_root(&tree);

    mpack_node_t content_node = mpack_node_map_cstr(root, "content");
    size_t content_len = mpack_node_strlen(content_node);
    if (content_len >= MAX_MESH_MESSAGE_LEN) {
        content_len = MAX_MESH_MESSAGE_LEN - 1;
    }
    memcpy(msg->message, mpack_node_str(content_node), content_len);
    msg->message[content_len] = '\0';

    msg->node_name[0] = '\0';
    mpack_node_t name_node = mpack_node_map_cstr(root, "node_name");
    if (!mpack_node_is_missing(name_node) && !mpack_node_is_nil(name_node)) {
        size_t name_len = mpack_node_strlen(name_node);
        if (name_len > MESH_NOW_NODE_NAME_MAX) {
            name_len = MESH_NOW_NODE_NAME_MAX;
        }
        memcpy(msg->node_name, mpack_node_str(name_node), name_len);
        msg->node_name[name_len] = '\0';
    }

    msg->neighbor_count = 0;
    mpack_node_t macs_node = mpack_node_map_cstr(root, "neighbor_macs");
    mpack_node_t names_node = mpack_node_map_cstr(root, "neighbor_names");
    if (!mpack_node_is_missing(macs_node) && !mpack_node_is_nil(macs_node)) {
        size_t count = mpack_node_array_length(macs_node);
        if (count > MAX_BEACON_NEIGHBORS) {
            count = MAX_BEACON_NEIGHBORS;
        }
        msg->neighbor_count = (uint8_t)count;
        for (size_t i = 0; i < count; i++) {
            mpack_node_t mac = mpack_node_array_at(macs_node, i);
            size_t mac_len = mpack_node_bin_size(mac);
            if (mac_len > ESP_NOW_ETH_ALEN) {
                mac_len = ESP_NOW_ETH_ALEN;
            }
            memcpy(msg->neighbor_macs[i], mpack_node_bin_data(mac), mac_len);
            msg->neighbor_names[i][0] = '\0';
            if (!mpack_node_is_missing(names_node) &&
                !mpack_node_is_nil(names_node)) {
                mpack_node_t nn = mpack_node_array_at(names_node, i);
                size_t nn_len = mpack_node_strlen(nn);
                if (nn_len > MESH_NOW_NODE_NAME_MAX) {
                    nn_len = MESH_NOW_NODE_NAME_MAX;
                }
                memcpy(msg->neighbor_names[i], mpack_node_str(nn), nn_len);
                msg->neighbor_names[i][nn_len] = '\0';
            }
        }
    }

    mpack_tree_destroy(&tree);
    return true;
}

// --- Fixed wire header ---
// Every frame shares the same 32-byte header; these three helpers are the
// single place that touches its layout.

static void wire_write_u32(uint8_t *wire, size_t pos, uint32_t v)
{
    wire[pos] = (uint8_t)(v >> 0);
    wire[pos + 1] = (uint8_t)(v >> 8);
    wire[pos + 2] = (uint8_t)(v >> 16);
    wire[pos + 3] = (uint8_t)(v >> 24);
}

static uint32_t wire_read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static size_t wire_write_header(uint8_t *wire, size_t pos,
                                const mesh_wire_header_t *hdr)
{
    wire[pos++] = MESH_NOW_MAGIC_0;
    wire[pos++] = MESH_NOW_MAGIC_1;
    wire[pos++] = MESH_NOW_WIRE_VERSION;
    wire[pos++] = hdr->flags;
    wire[pos++] = hdr->type;
    wire[pos++] = hdr->group_id;
    wire[pos++] = hdr->hop_limit;
    wire[pos++] = hdr->hop_count;
    wire_write_u32(wire, pos, hdr->message_id);
    pos += 4;
    wire_write_u32(wire, pos, hdr->reply_to);
    pos += 4;
    memcpy(wire + pos, hdr->sender_mac, ESP_NOW_ETH_ALEN);
    pos += ESP_NOW_ETH_ALEN;
    memcpy(wire + pos, hdr->target_mac, ESP_NOW_ETH_ALEN);
    pos += ESP_NOW_ETH_ALEN;
    wire_write_u32(wire, pos, hdr->timestamp);
    pos += 4;
    return pos;
}

// data[0..1] magic and data[2] version are validated by the caller first.
static void wire_parse_header(const uint8_t *data, mesh_wire_header_t *hdr)
{
    size_t pos = 3;
    hdr->flags = data[pos++];
    hdr->type = data[pos++];
    hdr->group_id = data[pos++];
    hdr->hop_limit = data[pos++];
    hdr->hop_count = data[pos++];
    hdr->message_id = wire_read_u32(data + pos);
    pos += 4;
    hdr->reply_to = wire_read_u32(data + pos);
    pos += 4;
    memcpy(hdr->sender_mac, data + pos, ESP_NOW_ETH_ALEN);
    pos += ESP_NOW_ETH_ALEN;
    memcpy(hdr->target_mac, data + pos, ESP_NOW_ETH_ALEN);
    pos += ESP_NOW_ETH_ALEN;
    hdr->timestamp = wire_read_u32(data + pos);
}

static void wire_apply_header(mesh_message_t *msg,
                              const mesh_wire_header_t *hdr)
{
    msg->flags = hdr->flags;
    msg->type = hdr->type;
    msg->group_id = hdr->group_id;
    msg->hop_limit = hdr->hop_limit;
    msg->hop_count = hdr->hop_count;
    msg->message_id = hdr->message_id;
    msg->reply_to = hdr->reply_to;
    memcpy(msg->sender_mac, hdr->sender_mac, ESP_NOW_ETH_ALEN);
    memcpy(msg->target_mac, hdr->target_mac, ESP_NOW_ETH_ALEN);
    msg->timestamp = hdr->timestamp;
}

esp_err_t mesh_now_prepare_wire(const mesh_message_t *msg, uint8_t *wire,
                                size_t *wire_len, bool do_encrypt)
{
    uint8_t plaintext[WIRE_BUF_SIZE];
    size_t pt_len = mesh_now_encode(msg, plaintext, sizeof(plaintext));
    if (pt_len == 0) {
        return ESP_FAIL;
    }

    mesh_wire_header_t hdr;
    uint8_t flags = msg->flags & ~(MSG_FLAG_ENCRYPTED | MSG_FLAG_HAS_NODE_NAME);
    if (do_encrypt && encryption_enabled && encryption_key_len > 0 &&
        !mesh_now_is_control_type(msg->type)) {
        flags |= MSG_FLAG_ENCRYPTED;
    }
    char name[MESH_NOW_NODE_NAME_MAX + 1];
    copy_local_name(name, sizeof(name));
    bool add_name = (name[0] != '\0') && mesh_now_carries_node_name(msg->type);
    if (add_name) {
        flags |= MSG_FLAG_HAS_NODE_NAME;
    }
    hdr.flags = flags;
    hdr.type = msg->type;
    hdr.group_id = msg->group_id;
    hdr.hop_limit = msg->hop_limit;
    hdr.hop_count = msg->hop_count;
    hdr.message_id = msg->message_id;
    hdr.reply_to = msg->reply_to;
    memcpy(hdr.sender_mac, msg->sender_mac, ESP_NOW_ETH_ALEN);
    memcpy(hdr.target_mac, msg->target_mac, ESP_NOW_ETH_ALEN);
    hdr.timestamp = msg->timestamp;

    size_t pos = wire_write_header(wire, 0, &hdr);

    if (flags & MSG_FLAG_ENCRYPTED) {
        uint8_t nonce[AES_GCM_NONCE_LEN];
        mesh_now_build_nonce(msg->message_id, msg->sender_mac, nonce);

        uint8_t aad[32];
        size_t aad_len = mesh_now_build_aad(msg, aad);

        uint8_t tag[AES_GCM_TAG_LEN];
        uint8_t ciphertext[WIRE_BUF_SIZE];
        esp_err_t err =
            mesh_now_aes_gcm_encrypt(plaintext, pt_len, aad, aad_len,
                                     encryption_key, nonce, ciphertext, tag);
        if (err != ESP_OK) {
            return err;
        }

        memcpy(wire + pos, nonce, AES_GCM_NONCE_LEN);
        pos += AES_GCM_NONCE_LEN;
        memcpy(wire + pos, ciphertext, pt_len);
        pos += pt_len;
        memcpy(wire + pos, tag, AES_GCM_TAG_LEN);
        pos += AES_GCM_TAG_LEN;
    } else {
        memcpy(wire + pos, plaintext, pt_len);
        pos += pt_len;
    }

    *wire_len = pos;
    return ESP_OK;
}

bool mesh_now_decode_wire(const uint8_t *data, size_t len, mesh_message_t *msg)
{
    if (len < MESH_NOW_HEADER_LEN) {
        return false;
    }
    if (data[0] != MESH_NOW_MAGIC_0 || data[1] != MESH_NOW_MAGIC_1) {
        return false;
    }
    if (data[2] != MESH_NOW_WIRE_VERSION) {
        return false;
    }

    mesh_wire_header_t hdr;
    wire_parse_header(data, &hdr);

    memset(msg, 0, sizeof(mesh_message_t));
    msg->node_name[0] = '\0';

    size_t pos = MESH_NOW_HEADER_LEN;

    if (hdr.flags & MSG_FLAG_ENCRYPTED) {
        if (encryption_key_len == 0 || !encryption_enabled) {
            ESP_LOGW(TAG, "Received encrypted message but no key set");
            return false;
        }
        if (pos + AES_GCM_NONCE_LEN + AES_GCM_TAG_LEN > len) {
            return false;
        }

        uint8_t nonce[AES_GCM_NONCE_LEN];
        memcpy(nonce, data + pos, AES_GCM_NONCE_LEN);
        pos += AES_GCM_NONCE_LEN;

        size_t ct_len = len - pos - AES_GCM_TAG_LEN;
        uint8_t tag[AES_GCM_TAG_LEN];
        memcpy(tag, data + pos + ct_len, AES_GCM_TAG_LEN);

        uint8_t aad[32];
        mesh_message_t aad_msg;
        memset(&aad_msg, 0, sizeof(aad_msg));
        aad_msg.type = hdr.type;
        memcpy(aad_msg.sender_mac, hdr.sender_mac, ESP_NOW_ETH_ALEN);
        memcpy(aad_msg.target_mac, hdr.target_mac, ESP_NOW_ETH_ALEN);
        aad_msg.group_id = hdr.group_id;
        aad_msg.timestamp = hdr.timestamp;
        size_t aad_len = mesh_now_build_aad(&aad_msg, aad);

        uint8_t plaintext[WIRE_BUF_SIZE];
        esp_err_t err =
            mesh_now_aes_gcm_decrypt(data + pos, ct_len, aad, aad_len,
                                     encryption_key, nonce, tag, plaintext);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "AES-GCM decryption failed for message %u",
                     hdr.message_id);
            return false;
        }

        bool decoded = mesh_now_decode(plaintext, ct_len, msg);
        if (!decoded) {
            return false;
        }
        // Transmit-only flag; never surfaces in the parsed message.
        hdr.flags &= ~MSG_FLAG_ENCRYPTED;
        wire_apply_header(msg, &hdr);
    } else {
        size_t payload_len = len - pos;
        if (payload_len > 0) {
            bool decoded = mesh_now_decode(data + pos, payload_len, msg);
            if (!decoded) {
                return false;
            }
            wire_apply_header(msg, &hdr);
        }
    }

    return true;
}
