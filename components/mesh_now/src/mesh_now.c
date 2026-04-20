#include "mesh_now.h"
#include "message_queue.h"
#include <esp_log.h>
#include <esp_now.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stddef.h>
#include <string.h>

#define TAG "MESH_NOW"
#define BEACON_INTERVAL_MS 5000  // Broadcast presence every 5 seconds
#define RETRANSMIT_TIMEOUT_MS 2000
#define MAX_PENDING_MESSAGES 16
#define MAX_SEEN_MESSAGE_IDS 128
#define MAX_ENCRYPTION_KEY 32
#define MAX_GROUP_ID 255

static mesh_peer_t peers[MAX_PEERS];
static int peer_count = 0;
static uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = BROADCAST_MAC;
static TaskHandle_t beacon_task_handle = NULL;
static TaskHandle_t retransmit_task_handle = NULL;
static mesh_now_receive_callback_t receive_callback = NULL;
static bool encryption_enabled = false;
static uint8_t encryption_key[MAX_ENCRYPTION_KEY];
static size_t encryption_key_len = 0;
static uint32_t next_message_id = 1;
static uint8_t local_group_id = 0;

typedef struct {
    bool active;
    mesh_message_t msg;
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
        next_message_id = 1;
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

    memmove(&seen_message_ids[0], &seen_message_ids[1], (MAX_SEEN_MESSAGE_IDS - 1) * sizeof(uint32_t));
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
        if (pending_messages[i].active && pending_messages[i].msg.message_id == message_id) {
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

static void mesh_now_crypt_payload(uint8_t *data, size_t len)
{
    if (!encryption_enabled || encryption_key_len == 0) {
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        data[i] ^= encryption_key[i % encryption_key_len];
    }
}

static void mesh_now_maybe_encrypt_message(mesh_message_t *msg)
{
    if (encryption_enabled && encryption_key_len > 0 && msg->type != MSG_TYPE_ACK && msg->type != MSG_TYPE_BEACON) {
        msg->flags |= MSG_FLAG_ENCRYPTED;
        mesh_now_crypt_payload((uint8_t *)msg->message, sizeof(msg->message));
    }
}

static esp_err_t mesh_now_queue_packet(const uint8_t *dest_mac, mesh_message_t *msg)
{
    int index = mesh_now_allocate_pending();
    if (index < 0) {
        ESP_LOGW(TAG, "No pending slots available for message %u", msg->message_id);
        return ESP_ERR_NO_MEM;
    }

    pending_messages[index].active = true;
    pending_messages[index].msg = *msg;
    memcpy(pending_messages[index].dest_mac, dest_mac, ESP_NOW_ETH_ALEN);
    pending_messages[index].retries = 0;
    pending_messages[index].last_send_time_ms = esp_timer_get_time() / 1000;

    return ESP_OK;
}

static esp_err_t mesh_now_send_packet(const uint8_t *dest_mac, mesh_message_t *msg, bool queue_for_retransmit)
{
    if (!(msg->flags & MSG_FLAG_ENCRYPTED)) {
        mesh_now_maybe_encrypt_message(msg);
    }

    if (queue_for_retransmit) {
        esp_err_t queue_err = mesh_now_queue_packet(dest_mac, msg);
        if (queue_err != ESP_OK) {
            return queue_err;
        }
    }

    esp_err_t ret = esp_now_send(dest_mac, (uint8_t *)msg, sizeof(mesh_message_t));
    if (ret != ESP_OK && queue_for_retransmit) {
        int index = mesh_now_find_pending(msg->message_id);
        if (index >= 0) {
            mesh_now_release_pending(index);
        }
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
    if (forward.hop_count == 0) {
        return;
    }

    esp_err_t ret = esp_now_send(broadcast_mac, (uint8_t *)&forward, sizeof(mesh_message_t));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to route message %u: %s", forward.message_id, esp_err_to_name(ret));
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

    esp_err_t ret = esp_now_send(broadcast_mac, (uint8_t *)&ack_msg, sizeof(mesh_message_t));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send ACK for message %u: %s", received_msg->message_id, esp_err_to_name(ret));
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
                ESP_LOGW(TAG, "Dropping message %u after %d retries", pending->msg.message_id, pending->retries);
                pending->active = false;
                continue;
            }

            pending->retries++;
            pending->last_send_time_ms = now_ms;
            esp_err_t ret = esp_now_send(pending->dest_mac, (uint8_t *)&pending->msg, sizeof(mesh_message_t));
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Retransmitted message %u (retry %d)", pending->msg.message_id, pending->retries);
            } else {
                ESP_LOGW(TAG, "Retransmit failed for %u: %s", pending->msg.message_id, esp_err_to_name(ret));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ESP-NOW send callback
static void esp_now_send_cb(const esp_now_send_info_t *send_info, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS)
    {
        ESP_LOGI(TAG, "Message sent successfully to %02x:%02x:%02x:%02x:%02x:%02x",
                 send_info->des_addr[0], send_info->des_addr[1], send_info->des_addr[2],
                 send_info->des_addr[3], send_info->des_addr[4], send_info->des_addr[5]);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to send message to %02x:%02x:%02x:%02x:%02x:%02x",
                 send_info->des_addr[0], send_info->des_addr[1], send_info->des_addr[2],
                 send_info->des_addr[3], send_info->des_addr[4], send_info->des_addr[5]);
    }
}

// ESP-NOW receive callback
static void esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (len != sizeof(mesh_message_t))
    {
        ESP_LOGW(TAG, "Received invalid message length: %d (expected %d)", len, sizeof(mesh_message_t));
        return;
    }

    mesh_message_t mesh_msg;
    memcpy(&mesh_msg, data, sizeof(mesh_message_t));

    bool payload_encrypted = false;
    if (mesh_msg.flags & MSG_FLAG_ENCRYPTED) {
        payload_encrypted = true;
        mesh_now_crypt_payload((uint8_t *)mesh_msg.message, sizeof(mesh_msg.message));
        mesh_msg.flags &= ~MSG_FLAG_ENCRYPTED;
    }

    ESP_LOGI(TAG, "Received ESP-NOW message from %02x:%02x:%02x:%02x:%02x:%02x, type: %d, id: %u",
             recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
             recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5],
             mesh_msg.type, mesh_msg.message_id);

    if (mesh_msg.type != MSG_TYPE_BEACON && mesh_msg.type != MSG_TYPE_ACK) {
        if (mesh_now_is_message_seen(mesh_msg.message_id)) {
            ESP_LOGW(TAG, "Duplicate message %u ignored", mesh_msg.message_id);
            return;
        }
        mesh_now_mark_message_seen(mesh_msg.message_id);
    }

    if (mesh_msg.type == MSG_TYPE_BEACON)
    {
        ESP_LOGI(TAG, "Received discovery beacon from %02x:%02x:%02x:%02x:%02x:%02x",
                 mesh_msg.sender_mac[0], mesh_msg.sender_mac[1], mesh_msg.sender_mac[2],
                 mesh_msg.sender_mac[3], mesh_msg.sender_mac[4], mesh_msg.sender_mac[5]);
        mesh_now_add_peer(mesh_msg.sender_mac);
    }
    else if (mesh_msg.type == MSG_TYPE_ACK)
    {
        uint8_t my_mac[ESP_NOW_ETH_ALEN];
        esp_read_mac(my_mac, ESP_MAC_WIFI_STA);

        if (memcmp(mesh_msg.target_mac, my_mac, ESP_NOW_ETH_ALEN) != 0)
        {
            if (mesh_msg.hop_count > 0) {
                if (payload_encrypted) {
                    mesh_now_maybe_encrypt_message(&mesh_msg);
                }
                mesh_now_route_message(&mesh_msg);
            }
            return;
        }

        int pending_index = mesh_now_find_pending(mesh_msg.message_id);
        if (pending_index >= 0) {
            mesh_now_release_pending(pending_index);
            ESP_LOGI(TAG, "Received ACK for message %u", mesh_msg.message_id);
        }
    }
    else if (mesh_msg.type == MSG_TYPE_CHAT)
    {
        mesh_now_add_peer(mesh_msg.sender_mac);

        if (receive_callback) {
            receive_callback(&mesh_msg);
        } else {
            message_t msg;
            strncpy(msg.message, mesh_msg.message, sizeof(msg.message));
            memcpy(msg.sender_mac, mesh_msg.sender_mac, sizeof(msg.sender_mac));
            msg.timestamp = mesh_msg.timestamp;
            message_queue_send(&msg);
            ESP_LOGI(TAG, "Queued chat message: %s", mesh_msg.message);
        }

        if (mesh_msg.hop_count > 0) {
            if (payload_encrypted) {
                mesh_now_maybe_encrypt_message(&mesh_msg);
            }
            mesh_now_route_message(&mesh_msg);
        }
    }
    else if (mesh_msg.type == MSG_TYPE_DIRECT)
    {
        uint8_t my_mac[ESP_NOW_ETH_ALEN];
        esp_read_mac(my_mac, ESP_MAC_WIFI_STA);

        if (memcmp(mesh_msg.target_mac, my_mac, ESP_NOW_ETH_ALEN) != 0)
        {
            if (mesh_msg.hop_count > 0) {
                mesh_now_route_message(&mesh_msg);
            }
            return;
        }

        mesh_now_add_peer(mesh_msg.sender_mac);
        mesh_now_send_ack(&mesh_msg);

        if (receive_callback) {
            receive_callback(&mesh_msg);
        } else {
            message_t msg;
            strncpy(msg.message, mesh_msg.message, sizeof(msg.message));
            memcpy(msg.sender_mac, mesh_msg.sender_mac, sizeof(msg.sender_mac));
            msg.timestamp = mesh_msg.timestamp;
            message_queue_send(&msg);
            ESP_LOGI(TAG, "Queued direct message: %s", mesh_msg.message);
        }
    }
    else if (mesh_msg.type == MSG_TYPE_GROUP)
    {
        mesh_now_add_peer(mesh_msg.sender_mac);
        if (mesh_msg.group_id == local_group_id) {
            if (receive_callback) {
                receive_callback(&mesh_msg);
            } else {
                message_t msg;
                strncpy(msg.message, mesh_msg.message, sizeof(msg.message));
                memcpy(msg.sender_mac, mesh_msg.sender_mac, sizeof(msg.sender_mac));
                msg.timestamp = mesh_msg.timestamp;
                message_queue_send(&msg);
                ESP_LOGI(TAG, "Queued group message: %s", mesh_msg.message);
            }
        }

        if (mesh_msg.hop_count > 0) {
            if (payload_encrypted) {
                mesh_now_maybe_encrypt_message(&mesh_msg);
            }
            mesh_now_route_message(&mesh_msg);
        }
    }
    else if (mesh_msg.type == MSG_TYPE_PRESENCE)
    {
        mesh_now_add_peer(mesh_msg.sender_mac);
        if (receive_callback) {
            receive_callback(&mesh_msg);
        }
        if (mesh_msg.hop_count > 0) {
            if (payload_encrypted) {
                mesh_now_maybe_encrypt_message(&mesh_msg);
            }
            mesh_now_route_message(&mesh_msg);
        }
    }
    else if (mesh_msg.type == MSG_TYPE_TYPING)
    {
        uint8_t my_mac[ESP_NOW_ETH_ALEN];
        esp_read_mac(my_mac, ESP_MAC_WIFI_STA);

        if (memcmp(mesh_msg.target_mac, my_mac, ESP_NOW_ETH_ALEN) == 0) {
            if (receive_callback) {
                receive_callback(&mesh_msg);
            }
        } else if (mesh_msg.hop_count > 0) {
            if (payload_encrypted) {
                mesh_now_maybe_encrypt_message(&mesh_msg);
            }
            mesh_now_route_message(&mesh_msg);
        }
    }
}

// Beacon broadcast task - periodically announce presence to discover other nodes
static void beacon_task(void *pvParameters)
{
    mesh_message_t beacon;
    memset(&beacon, 0, sizeof(mesh_message_t));
    beacon.type = MSG_TYPE_BEACON;
    esp_read_mac(beacon.sender_mac, ESP_MAC_WIFI_STA);
    strcpy(beacon.message, "MESH-NOW-BEACON");

    ESP_LOGI(TAG, "Beacon task started, broadcasting every %d ms", BEACON_INTERVAL_MS);

    while (1)
    {
        beacon.timestamp = esp_timer_get_time() / 1000;

        // Send beacon to broadcast address for peer discovery
        esp_err_t ret = esp_now_send(broadcast_mac, (uint8_t *)&beacon, sizeof(mesh_message_t));
        if (ret == ESP_OK)
        {
            ESP_LOGD(TAG, "Beacon broadcast sent");
        }
        else
        {
            ESP_LOGW(TAG, "Beacon broadcast failed: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(BEACON_INTERVAL_MS));
    }
}

esp_err_t mesh_now_init(void)
{
    ESP_LOGI(TAG, "Initializing ESP-NOW mesh networking");

    // Initialize ESP-NOW
    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register callbacks
    ret = esp_now_register_send_cb(esp_now_send_cb);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register send callback: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_now_register_recv_cb(esp_now_recv_cb);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register receive callback: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add broadcast peer
    esp_now_peer_info_t broadcast_peer;
    memset(&broadcast_peer, 0, sizeof(esp_now_peer_info_t));
    memcpy(broadcast_peer.peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
    broadcast_peer.channel = 1;
    broadcast_peer.encrypt = false;

    ret = esp_now_add_peer(&broadcast_peer);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add broadcast peer: %s", esp_err_to_name(ret));
        return ret;
    }

    // Start beacon broadcast task for peer discovery
    BaseType_t task_ret = xTaskCreate(
        beacon_task,
        "beacon_task",
        4096,
        NULL,
        5,
        &beacon_task_handle);

    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create beacon task");
        return ESP_FAIL;
    }

    task_ret = xTaskCreate(
        retransmit_task,
        "retransmit_task",
        4096,
        NULL,
        5,
        &retransmit_task_handle);

    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create retransmit task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ESP-NOW mesh networking initialized successfully");
    return ESP_OK;
}

esp_err_t mesh_now_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing ESP-NOW mesh networking");

    // Stop beacon task
    if (beacon_task_handle != NULL)
    {
        vTaskDelete(beacon_task_handle);
        beacon_task_handle = NULL;
    }

    if (retransmit_task_handle != NULL)
    {
        vTaskDelete(retransmit_task_handle);
        retransmit_task_handle = NULL;
    }

    // Remove broadcast peer
    esp_now_del_peer(broadcast_mac);

    // Unregister callbacks
    esp_now_unregister_send_cb();
    esp_now_unregister_recv_cb();

    // Deinitialize ESP-NOW
    esp_err_t ret = esp_now_deinit();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to deinitialize ESP-NOW: %s", esp_err_to_name(ret));
        return ret;
    }

    // Clear peer list
    peer_count = 0;

    ESP_LOGI(TAG, "ESP-NOW mesh networking deinitialized successfully");
    return ESP_OK;
}

void mesh_now_add_peer(const uint8_t *mac)
{
    // Ignore adding our own MAC as a peer
    uint8_t my_mac[ESP_NOW_ETH_ALEN];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    if (memcmp(my_mac, mac, ESP_NOW_ETH_ALEN) == 0)
    {
        ESP_LOGD(TAG, "Ignoring add_peer for our own MAC");
        return;
    }

    // If peer already exists in esp-now subsystem, skip
    if (esp_now_is_peer_exist(mac))
    {
        ESP_LOGD(TAG, "Peer already exists in ESP-NOW subsystem: %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        // Ensure it's also in our local list
        for (int i = 0; i < peer_count; i++)
        {
            if (memcmp(peers[i].peer_addr, mac, ESP_NOW_ETH_ALEN) == 0)
            {
                return;
            }
        }
    }

    if (peer_count >= MAX_PEERS)
    {
        ESP_LOGW(TAG, "Max peers reached, cannot add peer: %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return;
    }

    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
    peer.channel = 1; // ESPNOW_CHANNEL
    peer.encrypt = false;

    esp_err_t rc = esp_now_add_peer(&peer);
    if (rc != ESP_OK && rc != ESP_ERR_ESPNOW_EXIST)
    {
        ESP_LOGW(TAG, "esp_now_add_peer failed: %s for %02x:%02x:%02x:%02x:%02x:%02x",
                 esp_err_to_name(rc), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return;
    }

    // Add to local list only if not already present
    for (int i = 0; i < peer_count; i++)
    {
        if (memcmp(peers[i].peer_addr, mac, ESP_NOW_ETH_ALEN) == 0)
        {
            return;
        }
    }

    memcpy(peers[peer_count].peer_addr, mac, ESP_NOW_ETH_ALEN);
    peers[peer_count].active = true;
    peer_count++;

    ESP_LOGI(TAG, "Added peer: %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void mesh_now_remove_peer(const uint8_t *mac)
{
    for (int i = 0; i < peer_count; i++)
    {
        if (memcmp(peers[i].peer_addr, mac, ESP_NOW_ETH_ALEN) == 0)
        {
            esp_err_t rc = esp_now_del_peer(mac);
            if (rc != ESP_OK && rc != ESP_ERR_ESPNOW_NOT_FOUND)
            {
                ESP_LOGW(TAG, "esp_now_del_peer failed: %s for %02x:%02x:%02x:%02x:%02x:%02x",
                         esp_err_to_name(rc), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }

            // Shift remaining peers
            for (int j = i; j < peer_count - 1; j++)
            {
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

static esp_err_t mesh_now_send_message_packet(mesh_message_t *msg, bool queue_for_retransmit)
{
    msg->message_id = mesh_now_generate_message_id();
    msg->flags &= ~MSG_FLAG_ENCRYPTED;
    msg->hop_count = DEFAULT_ROUTE_TTL;
    esp_read_mac(msg->sender_mac, ESP_MAC_WIFI_STA);
    msg->timestamp = esp_timer_get_time() / 1000;

    esp_err_t ret = mesh_now_send_packet(broadcast_mac, msg, queue_for_retransmit);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sent message type %d id %u", msg->type, msg->message_id);
    }
    return ret;
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
    if (target_mac == NULL || message == NULL)
    {
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
    if (target_mac == NULL)
    {
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
    if (key == NULL || len == 0 || len > MAX_ENCRYPTION_KEY)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(encryption_key, key, len);
    encryption_key_len = len;
    encryption_enabled = true;
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