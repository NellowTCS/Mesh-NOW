#include "mesh_now.h"
#include "message_queue.h"
#include <esp_log.h>
#include <esp_now.h>
#include <esp_idf_version.h>
#include <esp_random.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/gcm.h>
#include <aes/esp_aes_gcm.h>
#include <mpack.h>
#include <stddef.h>
#include <string.h>

#define TAG "MESH_NOW"
#define BEACON_INTERVAL_MS 5000
#define RETRANSMIT_TIMEOUT_MS 2000
#define MAX_PENDING_MESSAGES 16
#define MAX_SEEN_MESSAGE_IDS 128
#define MAX_ENCRYPTION_KEY 32
#define MAX_GROUP_ID 255
#define PEER_EXPIRY_US (30LL * 1000000LL)
#define AES_GCM_TAG_LEN 16
#define AES_GCM_NONCE_LEN 12
#define WIRE_BUF_SIZE 256

static mesh_peer_t peers[MAX_PEERS];
static int peer_count = 0;
static uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = BROADCAST_MAC;
static TaskHandle_t beacon_task_handle = NULL;
static TaskHandle_t retransmit_task_handle = NULL;
static mesh_now_receive_callback_t receive_callback = NULL;
static bool encryption_enabled = false;
static uint8_t encryption_key[MAX_ENCRYPTION_KEY];
static size_t encryption_key_len = 0;
static uint32_t next_message_id = 0;
static uint8_t local_group_id = 0;
static char local_node_name[MESH_NOW_NODE_NAME_MAX + 1] = {0};

typedef struct {
    bool active;
    uint32_t message_id;
    uint8_t wire_buf[WIRE_BUF_SIZE];
    size_t wire_len;
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];
    int retries;
    int64_t last_send_time_ms;
} pending_message_t;

static pending_message_t pending_messages[MAX_PENDING_MESSAGES];
static uint32_t seen_message_ids[MAX_SEEN_MESSAGE_IDS];
static int seen_message_count = 0;

static uint32_t mesh_now_generate_message_id(void)
{
    if (next_message_id == 0) {
        next_message_id = esp_random();
    }
    return next_message_id++;
}

static bool mesh_now_is_message_seen(uint32_t message_id)
{
    for (int i = 0; i < seen_message_count; ++i) {
        if (seen_message_ids[i] == message_id) {
            return true;
        }
    }
    return false;
}

static void mesh_now_mark_message_seen(uint32_t message_id)
{
    if (seen_message_count < MAX_SEEN_MESSAGE_IDS) {
        seen_message_ids[seen_message_count++] = message_id;
        return;
    }

    memmove(&seen_message_ids[0], &seen_message_ids[1],
            (MAX_SEEN_MESSAGE_IDS - 1) * sizeof(uint32_t));
    seen_message_ids[MAX_SEEN_MESSAGE_IDS - 1] = message_id;
}

static int mesh_now_allocate_pending(void)
{
    for (int i = 0; i < MAX_PENDING_MESSAGES; ++i) {
        if (!pending_messages[i].active) {
            return i;
        }
    }
    return -1;
}

static int mesh_now_find_pending(uint32_t message_id)
{
    for (int i = 0; i < MAX_PENDING_MESSAGES; ++i) {
        if (pending_messages[i].active &&
            pending_messages[i].message_id == message_id) {
            return i;
        }
    }
    return -1;
}

static void mesh_now_release_pending(int index)
{
    if (index >= 0 && index < MAX_PENDING_MESSAGES) {
        pending_messages[index].active = false;
    }
}

// Build AES-GCM nonce: message_id(4 LE) || sender_mac(8)
static void mesh_now_build_nonce(uint32_t message_id, const uint8_t *sender_mac,
                                 uint8_t nonce[AES_GCM_NONCE_LEN])
{
    nonce[0] = (message_id >> 0) & 0xff;
    nonce[1] = (message_id >> 8) & 0xff;
    nonce[2] = (message_id >> 16) & 0xff;
    nonce[3] = (message_id >> 24) & 0xff;
    memcpy(nonce + 4, sender_mac, 8);
}

// AES-128-GCM encrypt: plaintext -> ciphertext || tag(16)
static esp_err_t mesh_now_aes_gcm_encrypt(const uint8_t *plaintext, size_t pt_len,
                                           const uint8_t *aad, size_t aad_len,
                                           const uint8_t key[16],
                                           const uint8_t nonce[AES_GCM_NONCE_LEN],
                                           uint8_t *ciphertext,
                                           uint8_t tag[AES_GCM_TAG_LEN])
{
    esp_gcm_context ctx;
    esp_aes_gcm_init(&ctx);

    int ret = esp_aes_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret != 0) {
        esp_aes_gcm_free(&ctx);
        return ESP_FAIL;
    }

    ret = esp_aes_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT,
                                     pt_len, nonce, AES_GCM_NONCE_LEN,
                                     aad, aad_len,
                                     plaintext, ciphertext,
                                     AES_GCM_TAG_LEN, tag);
    esp_aes_gcm_free(&ctx);
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

// AES-128-GCM decrypt: ciphertext || tag(16) -> plaintext
static esp_err_t mesh_now_aes_gcm_decrypt(const uint8_t *ciphertext, size_t ct_len,
                                           const uint8_t *aad, size_t aad_len,
                                           const uint8_t key[16],
                                           const uint8_t nonce[AES_GCM_NONCE_LEN],
                                           const uint8_t tag[AES_GCM_TAG_LEN],
                                           uint8_t *plaintext)
{
    esp_gcm_context ctx;
    esp_aes_gcm_init(&ctx);

    int ret = esp_aes_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret != 0) {
        esp_aes_gcm_free(&ctx);
        return ESP_FAIL;
    }

    ret = esp_aes_gcm_auth_decrypt(&ctx, ct_len,
                                    nonce, AES_GCM_NONCE_LEN,
                                    aad, aad_len,
                                    tag, AES_GCM_TAG_LEN,
                                    ciphertext, plaintext);
    esp_aes_gcm_free(&ctx);
    return (ret == 0) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

// Build AAD from header fields (type, sender, target, group, timestamp)
static size_t mesh_now_build_aad(const mesh_message_t *msg, uint8_t *aad)
{
    size_t pos = 0;
    aad[pos++] = msg->type;
    memcpy(aad + pos, msg->sender_mac, ESP_NOW_ETH_ALEN);
    pos += ESP_NOW_ETH_ALEN;
    memcpy(aad + pos, msg->target_mac, ESP_NOW_ETH_ALEN);
    pos += ESP_NOW_ETH_ALEN;
    aad[pos++] = msg->group_id;
    aad[pos++] = (msg->timestamp >> 0) & 0xff;
    aad[pos++] = (msg->timestamp >> 8) & 0xff;
    aad[pos++] = (msg->timestamp >> 16) & 0xff;
    aad[pos++] = (msg->timestamp >> 24) & 0xff;
    return pos;
}

// Encode mesh_message_t to mpack buffer. Returns bytes written, 0 on error.
size_t mesh_now_encode(const mesh_message_t *msg, uint8_t *out, size_t out_size)
{
    mpack_writer_t writer;
    mpack_writer_init(&writer, (char *)out, out_size);

    mpack_start_map(&writer, 8);
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

    bool has_name = (local_node_name[0] != '\0') &&
                    (msg->type == MSG_TYPE_BEACON);
    if (has_name) {
        mpack_write_cstr(&writer, "node_name");
        mpack_write_str(&writer, local_node_name, strlen(local_node_name));
    }
    mpack_complete_map(&writer);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        return 0;
    }
    return mpack_writer_buffer_used(&writer);
}

// Decode mpack buffer to mesh_message_t. Returns true on success.
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

// Encode + optionally encrypt a message into a wire buffer
static esp_err_t mesh_now_prepare_wire(const mesh_message_t *msg,
                                        uint8_t *wire, size_t *wire_len,
                                        bool do_encrypt)
{
    uint8_t plaintext[WIRE_BUF_SIZE];
    size_t pt_len = mesh_now_encode(msg, plaintext, sizeof(plaintext));
    if (pt_len == 0) {
        return ESP_FAIL;
    }

    // Header: magic(2) + version(1) + flags(1) + type(1) + group_id(1) + hop_count(1)
    //       + message_id(4) + sender_mac(6) + target_mac(6) + timestamp(4) = 28 bytes
    size_t pos = 0;
    wire[pos++] = MESH_NOW_MAGIC_0;
    wire[pos++] = MESH_NOW_MAGIC_1;
    wire[pos++] = 1; // version
    uint8_t flags = msg->flags & ~(MSG_FLAG_ENCRYPTED | MSG_FLAG_HAS_NODE_NAME);
    if (do_encrypt && encryption_enabled && encryption_key_len > 0 &&
        msg->type != MSG_TYPE_ACK && msg->type != MSG_TYPE_BEACON) {
        flags |= MSG_FLAG_ENCRYPTED;
    }
    bool add_name = (local_node_name[0] != '\0') && (msg->type == MSG_TYPE_BEACON);
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

// Decode wire bytes into mesh_message_t. Returns true on success.
static bool mesh_now_decode_wire(const uint8_t *data, size_t len,
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

static void mesh_now_route_message(mesh_message_t *msg)
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

static void mesh_now_send_ack(const mesh_message_t *received_msg)
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
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
    }
}
#endif

static void mesh_now_handle_message(const mesh_message_t *mesh_msg)
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
static void esp_now_recv_cb(const esp_now_recv_info_t *recv_info,
                              const uint8_t *data, int len)
{
    const uint8_t *src_addr = recv_info->src_addr;
#else
static void esp_now_recv_cb(const uint8_t *mac_addr,
                              const uint8_t *data, int len)
{
    const uint8_t *src_addr = mac_addr;
#endif

    mesh_message_t mesh_msg;
    if (!mesh_now_decode_wire(data, len, &mesh_msg)) {
        ESP_LOGW(TAG, "Failed to decode wire message from "
                 "%02x:%02x:%02x:%02x:%02x:%02x (len=%d)",
                 src_addr[0], src_addr[1], src_addr[2],
                 src_addr[3], src_addr[4], src_addr[5], len);
        return;
    }

    ESP_LOGI(TAG, "Received message from %02x:%02x:%02x:%02x:%02x:%02x, "
             "type: %d, id: %u",
             src_addr[0], src_addr[1], src_addr[2],
             src_addr[3], src_addr[4], src_addr[5],
             mesh_msg.type, mesh_msg.message_id);

    uint8_t my_mac[ESP_NOW_ETH_ALEN];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    if (memcmp(mesh_msg.sender_mac, my_mac, ESP_NOW_ETH_ALEN) == 0) {
        return;
    }

    if (mesh_msg.type != MSG_TYPE_BEACON && mesh_msg.type != MSG_TYPE_ACK) {
        if (mesh_now_is_message_seen(mesh_msg.message_id)) {
            ESP_LOGW(TAG, "Duplicate message %u ignored", mesh_msg.message_id);
            return;
        }
        mesh_now_mark_message_seen(mesh_msg.message_id);
    }

    if (mesh_msg.type == MSG_TYPE_BEACON) {
        ESP_LOGI(TAG, "Received beacon from %02x:%02x:%02x:%02x:%02x:%02x"
                 "%s%s",
                 mesh_msg.sender_mac[0], mesh_msg.sender_mac[1],
                 mesh_msg.sender_mac[2], mesh_msg.sender_mac[3],
                 mesh_msg.sender_mac[4], mesh_msg.sender_mac[5],
                 mesh_msg.node_name[0] ? " (" : "",
                 mesh_msg.node_name[0] ? mesh_msg.node_name : "");
        mesh_now_add_peer(mesh_msg.sender_mac);

        if (mesh_msg.node_name[0] != '\0') {
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
        }
    } else if (mesh_msg.type == MSG_TYPE_ACK) {
        if (memcmp(mesh_msg.target_mac, my_mac, ESP_NOW_ETH_ALEN) != 0) {
            if (mesh_msg.hop_count > 0) {
                mesh_now_route_message(&mesh_msg);
            }
            return;
        }

        int pending_index = mesh_now_find_pending(mesh_msg.message_id);
        if (pending_index >= 0) {
            mesh_now_release_pending(pending_index);
            ESP_LOGI(TAG, "Received ACK for message %u", mesh_msg.message_id);
        }
    } else if (mesh_msg.type == MSG_TYPE_CHAT) {
        mesh_now_add_peer(mesh_msg.sender_mac);
        mesh_now_handle_message(&mesh_msg);

        if (mesh_msg.hop_count > 0) {
            mesh_now_route_message(&mesh_msg);
        }
    } else if (mesh_msg.type == MSG_TYPE_DIRECT) {
        if (memcmp(mesh_msg.target_mac, my_mac, ESP_NOW_ETH_ALEN) != 0) {
            if (mesh_msg.hop_count > 0) {
                mesh_now_route_message(&mesh_msg);
            }
            return;
        }

        mesh_now_add_peer(mesh_msg.sender_mac);
        mesh_now_send_ack(&mesh_msg);
        mesh_now_handle_message(&mesh_msg);
    } else if (mesh_msg.type == MSG_TYPE_GROUP) {
        mesh_now_add_peer(mesh_msg.sender_mac);
        if (local_group_id != 0 && mesh_msg.group_id == local_group_id) {
            mesh_now_handle_message(&mesh_msg);
        }

        if (mesh_msg.hop_count > 0) {
            mesh_now_route_message(&mesh_msg);
        }
    } else if (mesh_msg.type == MSG_TYPE_PRESENCE) {
        mesh_now_add_peer(mesh_msg.sender_mac);
        mesh_now_handle_message(&mesh_msg);

        if (mesh_msg.hop_count > 0) {
            mesh_now_route_message(&mesh_msg);
        }
    } else if (mesh_msg.type == MSG_TYPE_TYPING) {
        if (memcmp(mesh_msg.target_mac, my_mac, ESP_NOW_ETH_ALEN) == 0) {
            mesh_now_handle_message(&mesh_msg);
        } else if (mesh_msg.hop_count > 0) {
            mesh_now_route_message(&mesh_msg);
        }
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

esp_err_t mesh_now_init(void)
{
    ESP_LOGI(TAG, "Initializing ESP-NOW mesh networking");

    message_queue_init();

    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_now_register_send_cb(esp_now_send_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register send callback: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = esp_now_register_recv_cb(esp_now_recv_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register recv callback: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    esp_now_peer_info_t broadcast_peer;
    memset(&broadcast_peer, 0, sizeof(esp_now_peer_info_t));
    memcpy(broadcast_peer.peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
    broadcast_peer.channel = 1;
    broadcast_peer.encrypt = false;

    ret = esp_now_add_peer(&broadcast_peer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add broadcast peer: %s",
                 esp_err_to_name(ret));
        return ret;
    }

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

    ESP_LOGI(TAG, "ESP-NOW mesh networking initialized successfully");
    return ESP_OK;
}

esp_err_t mesh_now_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing ESP-NOW mesh networking");

    if (beacon_task_handle != NULL) {
        vTaskDelete(beacon_task_handle);
        beacon_task_handle = NULL;
    }

    if (retransmit_task_handle != NULL) {
        vTaskDelete(retransmit_task_handle);
        retransmit_task_handle = NULL;
    }

    esp_now_del_peer(broadcast_mac);
    esp_now_unregister_send_cb();
    esp_now_unregister_recv_cb();

    esp_err_t ret = esp_now_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize ESP-NOW: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    peer_count = 0;
    ESP_LOGI(TAG, "ESP-NOW mesh networking deinitialized successfully");
    return ESP_OK;
}

void mesh_now_add_peer(const uint8_t *mac)
{
    uint8_t my_mac[ESP_NOW_ETH_ALEN];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    if (memcmp(my_mac, mac, ESP_NOW_ETH_ALEN) == 0) {
        return;
    }

    if (esp_now_is_peer_exist(mac)) {
        for (int i = 0; i < peer_count; i++) {
            if (memcmp(peers[i].peer_addr, mac, ESP_NOW_ETH_ALEN) == 0) {
                peers[i].last_seen = esp_timer_get_time();
                return;
            }
        }
    }

    if (peer_count >= MAX_PEERS) {
        ESP_LOGW(TAG, "Max peers reached, cannot add: %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return;
    }

    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
    peer.channel = 1;
    peer.encrypt = false;

    esp_err_t rc = esp_now_add_peer(&peer);
    if (rc != ESP_OK && rc != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGW(TAG, "esp_now_add_peer failed: %s",
                 esp_err_to_name(rc));
        return;
    }

    for (int i = 0; i < peer_count; i++) {
        if (memcmp(peers[i].peer_addr, mac, ESP_NOW_ETH_ALEN) == 0) {
            peers[i].last_seen = esp_timer_get_time();
            return;
        }
    }

    memcpy(peers[peer_count].peer_addr, mac, ESP_NOW_ETH_ALEN);
    peers[peer_count].active = true;
    peers[peer_count].last_seen = esp_timer_get_time();
    peers[peer_count].node_name[0] = '\0';
    peer_count++;

    ESP_LOGI(TAG, "Added peer: %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void mesh_now_remove_peer(const uint8_t *mac)
{
    for (int i = 0; i < peer_count; i++) {
        if (memcmp(peers[i].peer_addr, mac, ESP_NOW_ETH_ALEN) == 0) {
            esp_err_t rc = esp_now_del_peer(mac);
            if (rc != ESP_OK && rc != ESP_ERR_ESPNOW_NOT_FOUND) {
                ESP_LOGW(TAG, "esp_now_del_peer failed: %s",
                         esp_err_to_name(rc));
            }

            for (int j = i; j < peer_count - 1; j++) {
                peers[j] = peers[j + 1];
            }
            peer_count--;
            ESP_LOGI(TAG, "Removed peer: %02x:%02x:%02x:%02x:%02x:%02x",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return;
        }
    }
}

void mesh_now_set_receive_callback(mesh_now_receive_callback_t callback)
{
    receive_callback = callback;
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

esp_err_t mesh_now_set_group(uint8_t group_id)
{
    local_group_id = group_id;
    return ESP_OK;
}

esp_err_t mesh_now_set_encryption_key(const uint8_t *key, size_t len)
{
    if (key == NULL || len == 0 || len > MAX_ENCRYPTION_KEY) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(encryption_key, key, len);
    encryption_key_len = len;
    encryption_enabled = true;
    return ESP_OK;
}

esp_err_t mesh_now_set_name(const char *name)
{
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(local_node_name, name, MESH_NOW_NODE_NAME_MAX);
    local_node_name[MESH_NOW_NODE_NAME_MAX] = '\0';
    return ESP_OK;
}

int mesh_now_get_peer_count(void)
{
    return peer_count;
}

mesh_peer_t *mesh_now_get_peers(void)
{
    return peers;
}
