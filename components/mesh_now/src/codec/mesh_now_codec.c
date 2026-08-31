#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <mpack.h>
#include <string.h>

#define TAG "MESH_NOW"

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

    bool has_name = (name[0] != '\0') && (msg->type == MSG_TYPE_BEACON);
    mpack_build_map(&writer);
    mpack_write_cstr(&writer, "type");
    mpack_write_uint(&writer, msg->type);
    mpack_write_cstr(&writer, "flags");
    mpack_write_uint(&writer, msg->flags & ~MSG_FLAG_ENCRYPTED);
    mpack_write_cstr(&writer, "group_id");
    mpack_write_uint(&writer, msg->group_id);
    mpack_write_cstr(&writer, "hop_count");
    mpack_write_uint(&writer, msg->hop_count);
    mpack_write_cstr(&writer, "message_id");
    mpack_write_uint(&writer, msg->message_id);
    mpack_write_cstr(&writer, "sender");
    mpack_write_bin(&writer, (const char *)msg->sender_mac, ESP_NOW_ETH_ALEN);
    mpack_write_cstr(&writer, "target");
    mpack_write_bin(&writer, (const char *)msg->target_mac, ESP_NOW_ETH_ALEN);
    mpack_write_cstr(&writer, "timestamp");
    mpack_write_uint(&writer, msg->timestamp);

    size_t content_len = strnlen(msg->message, MAX_MESH_MESSAGE_LEN);
    mpack_write_cstr(&writer, "content");
    mpack_write_str(&writer, msg->message, content_len);

    if (has_name) {
        mpack_write_cstr(&writer, "node_name");
        mpack_write_str(&writer, name, strlen(name));
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

    memset(msg, 0, sizeof(mesh_message_t));

    msg->type = (uint8_t)mpack_node_u8(mpack_node_map_cstr(root, "type"));
    msg->flags = (uint8_t)mpack_node_u8(mpack_node_map_cstr(root, "flags"));
    msg->group_id = (uint8_t)mpack_node_u8(mpack_node_map_cstr(root, "group_id"));
    msg->hop_count = (uint8_t)mpack_node_u8(mpack_node_map_cstr(root, "hop_count"));
    msg->message_id = mpack_node_u32(mpack_node_map_cstr(root, "message_id"));
    msg->timestamp = mpack_node_u32(mpack_node_map_cstr(root, "timestamp"));

    mpack_node_t sender_node = mpack_node_map_cstr(root, "sender");
    size_t sender_len = mpack_node_bin_size(sender_node);
    if (sender_len == ESP_NOW_ETH_ALEN) {
        memcpy(msg->sender_mac, mpack_node_bin_data(sender_node), ESP_NOW_ETH_ALEN);
    }

    mpack_node_t target_node = mpack_node_map_cstr(root, "target");
    size_t target_len = mpack_node_bin_size(target_node);
    if (target_len == ESP_NOW_ETH_ALEN) {
        memcpy(msg->target_mac, mpack_node_bin_data(target_node), ESP_NOW_ETH_ALEN);
    }

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

    mpack_tree_destroy(&tree);
    return true;
}

esp_err_t mesh_now_prepare_wire(const mesh_message_t *msg,
                                 uint8_t *wire, size_t *wire_len,
                                 bool do_encrypt)
{
    uint8_t plaintext[WIRE_BUF_SIZE];
    size_t pt_len = mesh_now_encode(msg, plaintext, sizeof(plaintext));
    if (pt_len == 0) {
        return ESP_FAIL;
    }

    size_t pos = 0;
    wire[pos++] = MESH_NOW_MAGIC_0;
    wire[pos++] = MESH_NOW_MAGIC_1;
    wire[pos++] = 1;
    uint8_t flags = msg->flags & ~(MSG_FLAG_ENCRYPTED | MSG_FLAG_HAS_NODE_NAME);
    if (do_encrypt && encryption_enabled && encryption_key_len > 0 &&
        msg->type != MSG_TYPE_ACK && msg->type != MSG_TYPE_BEACON) {
        flags |= MSG_FLAG_ENCRYPTED;
    }
    char name[MESH_NOW_NODE_NAME_MAX + 1];
    copy_local_name(name, sizeof(name));
    bool add_name = (name[0] != '\0') && (msg->type == MSG_TYPE_BEACON);
    if (add_name) {
        flags |= MSG_FLAG_HAS_NODE_NAME;
    }
    wire[pos++] = flags;
    wire[pos++] = msg->type;
    wire[pos++] = msg->group_id;
    wire[pos++] = msg->hop_count;
    wire[pos++] = (msg->message_id >> 0) & 0xff;
    wire[pos++] = (msg->message_id >> 8) & 0xff;
    wire[pos++] = (msg->message_id >> 16) & 0xff;
    wire[pos++] = (msg->message_id >> 24) & 0xff;
    memcpy(wire + pos, msg->sender_mac, ESP_NOW_ETH_ALEN);
    pos += ESP_NOW_ETH_ALEN;
    memcpy(wire + pos, msg->target_mac, ESP_NOW_ETH_ALEN);
    pos += ESP_NOW_ETH_ALEN;
    wire[pos++] = (msg->timestamp >> 0) & 0xff;
    wire[pos++] = (msg->timestamp >> 8) & 0xff;
    wire[pos++] = (msg->timestamp >> 16) & 0xff;
    wire[pos++] = (msg->timestamp >> 24) & 0xff;

    if (flags & MSG_FLAG_ENCRYPTED) {
        uint8_t nonce[AES_GCM_NONCE_LEN];
        mesh_now_build_nonce(msg->message_id, msg->sender_mac, nonce);

        uint8_t aad[32];
        size_t aad_len = mesh_now_build_aad(msg, aad);

        uint8_t tag[AES_GCM_TAG_LEN];
        uint8_t ciphertext[WIRE_BUF_SIZE];
        esp_err_t err = mesh_now_aes_gcm_encrypt(plaintext, pt_len,
                                                  aad, aad_len,
                                                  encryption_key, nonce,
                                                  ciphertext, tag);
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

bool mesh_now_decode_wire(const uint8_t *data, size_t len,
                           mesh_message_t *msg)
{
    if (len < 28) {
        return false;
    }
    if (data[0] != MESH_NOW_MAGIC_0 || data[1] != MESH_NOW_MAGIC_1) {
        return false;
    }

    size_t pos = 2;
    uint8_t version = data[pos++];
    if (version != 1) {
        return false;
    }
    uint8_t flags = data[pos++];
    uint8_t msg_type = data[pos++];
    uint8_t group_id = data[pos++];
    uint8_t hop_count = data[pos++];

    uint32_t message_id = (uint32_t)data[pos] |
                          ((uint32_t)data[pos + 1] << 8) |
                          ((uint32_t)data[pos + 2] << 16) |
                          ((uint32_t)data[pos + 3] << 24);
    pos += 4;

    uint8_t sender_mac[ESP_NOW_ETH_ALEN];
    memcpy(sender_mac, data + pos, ESP_NOW_ETH_ALEN);
    pos += ESP_NOW_ETH_ALEN;

    uint8_t target_mac[ESP_NOW_ETH_ALEN];
    memcpy(target_mac, data + pos, ESP_NOW_ETH_ALEN);
    pos += ESP_NOW_ETH_ALEN;

    uint32_t timestamp = (uint32_t)data[pos] |
                         ((uint32_t)data[pos + 1] << 8) |
                         ((uint32_t)data[pos + 2] << 16) |
                         ((uint32_t)data[pos + 3] << 24);
    pos += 4;

    memset(msg, 0, sizeof(mesh_message_t));
    msg->type = msg_type;
    msg->flags = flags;
    msg->group_id = group_id;
    msg->hop_count = hop_count;
    msg->message_id = message_id;
    memcpy(msg->sender_mac, sender_mac, ESP_NOW_ETH_ALEN);
    memcpy(msg->target_mac, target_mac, ESP_NOW_ETH_ALEN);
    msg->timestamp = timestamp;
    msg->node_name[0] = '\0';

    if (flags & MSG_FLAG_ENCRYPTED) {
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
        aad_msg.type = msg_type;
        memcpy(aad_msg.sender_mac, sender_mac, ESP_NOW_ETH_ALEN);
        memcpy(aad_msg.target_mac, target_mac, ESP_NOW_ETH_ALEN);
        aad_msg.group_id = group_id;
        aad_msg.timestamp = timestamp;
        size_t aad_len = mesh_now_build_aad(&aad_msg, aad);

        uint8_t plaintext[WIRE_BUF_SIZE];
        esp_err_t err = mesh_now_aes_gcm_decrypt(data + pos, ct_len,
                                                   aad, aad_len,
                                                   encryption_key, nonce,
                                                   tag, plaintext);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "AES-GCM decryption failed for message %u", message_id);
            return false;
        }

        bool decoded = mesh_now_decode(plaintext, ct_len, msg);
        if (!decoded) {
            return false;
        }
        msg->type = msg_type;
        msg->flags = flags & ~MSG_FLAG_ENCRYPTED;
        msg->group_id = group_id;
        msg->hop_count = hop_count;
        msg->message_id = message_id;
        memcpy(msg->sender_mac, sender_mac, ESP_NOW_ETH_ALEN);
        memcpy(msg->target_mac, target_mac, ESP_NOW_ETH_ALEN);
        msg->timestamp = timestamp;
    } else {
        size_t payload_len = len - pos;
        if (payload_len > 0) {
            bool decoded = mesh_now_decode(data + pos, payload_len, msg);
            if (!decoded) {
                return false;
            }
            msg->type = msg_type;
            msg->flags = flags;
            msg->group_id = group_id;
            msg->hop_count = hop_count;
            msg->message_id = message_id;
            memcpy(msg->sender_mac, sender_mac, ESP_NOW_ETH_ALEN);
            memcpy(msg->target_mac, target_mac, ESP_NOW_ETH_ALEN);
            msg->timestamp = timestamp;
        }
    }

    return true;
}
