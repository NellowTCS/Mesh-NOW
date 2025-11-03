#include "mesh_now.h"
#include "message_queue.h"
#include <esp_log.h>
#include <esp_now.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#define TAG "MESH_NOW"
#define BEACON_INTERVAL_MS 5000  // Broadcast presence every 5 seconds

static mesh_peer_t peers[MAX_PEERS];
static int peer_count = 0;
static uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = BROADCAST_MAC;
static TaskHandle_t beacon_task_handle = NULL;

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

    ESP_LOGI(TAG, "Received ESP-NOW message from %02x:%02x:%02x:%02x:%02x:%02x, type: %d",
             recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
             recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5],
             mesh_msg.type);

    if (mesh_msg.type == MSG_TYPE_BEACON)
    {
        // Discovery beacon - add sender as peer if not already added
        ESP_LOGI(TAG, "Received discovery beacon from %02x:%02x:%02x:%02x:%02x:%02x",
                 mesh_msg.sender_mac[0], mesh_msg.sender_mac[1], mesh_msg.sender_mac[2],
                 mesh_msg.sender_mac[3], mesh_msg.sender_mac[4], mesh_msg.sender_mac[5]);
        mesh_now_add_peer(mesh_msg.sender_mac);
    }
    else if (mesh_msg.type == MSG_TYPE_CHAT)
    {
        // Chat message - add sender as peer and queue for web interface
        mesh_now_add_peer(mesh_msg.sender_mac);

        message_t msg;
        strncpy(msg.message, mesh_msg.message, sizeof(msg.message));
        memcpy(msg.sender_mac, mesh_msg.sender_mac, sizeof(msg.sender_mac));
        msg.timestamp = mesh_msg.timestamp;
        message_queue_send(&msg);

        ESP_LOGI(TAG, "Queued chat message: %s", mesh_msg.message);
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

esp_err_t mesh_now_send_message(const char *message)
{
    if (peer_count == 0)
    {
        ESP_LOGW(TAG, "No peers available, message not sent: %s", message);
        return ESP_FAIL;
    }

    // Create chat message
    mesh_message_t msg;
    memset(&msg, 0, sizeof(mesh_message_t));
    msg.type = MSG_TYPE_CHAT;
    strncpy(msg.message, message, sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    esp_read_mac(msg.sender_mac, ESP_MAC_WIFI_STA);
    msg.timestamp = esp_timer_get_time() / 1000; // Convert to ms

    // Send to all peers
    esp_err_t ret = ESP_OK;
    for (int i = 0; i < peer_count; i++)
    {
        esp_err_t send_ret = esp_now_send(peers[i].peer_addr, (uint8_t *)&msg, sizeof(mesh_message_t));
        if (send_ret != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to send to peer %d: %s", i, esp_err_to_name(send_ret));
            ret = send_ret;
        }
    }

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Sent chat message to %d peers: %s", peer_count, message);
    }

    return ret;
}

int mesh_now_get_peer_count(void)
{
    return peer_count;
}

mesh_peer_t *mesh_now_get_peers(void)
{
    return peers;
}