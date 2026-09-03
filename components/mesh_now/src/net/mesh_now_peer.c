#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <string.h>

#define TAG "MESH_NOW"

void mesh_now_add_peer(const uint8_t *mac)
{
    uint8_t my_mac[ESP_NOW_ETH_ALEN];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    if (memcmp(my_mac, mac, ESP_NOW_ETH_ALEN) == 0) {
        return;
    }

    xSemaphoreTake(state_mutex, portMAX_DELAY);

    if (esp_now_is_peer_exist(mac)) {
        for (int i = 0; i < peer_count; i++) {
            if (memcmp(peers[i].peer_addr, mac, ESP_NOW_ETH_ALEN) == 0) {
                peers[i].last_seen = esp_timer_get_time();
                xSemaphoreGive(state_mutex);
                return;
            }
        }
    }

    if (peer_count >= MAX_PEERS) {
        ESP_LOGW(TAG,
                 "Max peers reached, cannot add: %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        xSemaphoreGive(state_mutex);
        return;
    }

    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
    peer.channel = 1;
    peer.encrypt = false;

    esp_err_t rc = esp_now_add_peer(&peer);
    if (rc != ESP_OK && rc != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGW(TAG, "esp_now_add_peer failed: %s", esp_err_to_name(rc));
        xSemaphoreGive(state_mutex);
        return;
    }

    for (int i = 0; i < peer_count; i++) {
        if (memcmp(peers[i].peer_addr, mac, ESP_NOW_ETH_ALEN) == 0) {
            peers[i].last_seen = esp_timer_get_time();
            xSemaphoreGive(state_mutex);
            return;
        }
    }

    memcpy(peers[peer_count].peer_addr, mac, ESP_NOW_ETH_ALEN);
    peers[peer_count].active = true;
    peers[peer_count].last_seen = esp_timer_get_time();
    peers[peer_count].node_name[0] = '\0';
    peer_count++;

    xSemaphoreGive(state_mutex);

    ESP_LOGI(TAG, "Added peer: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
}

void mesh_now_remove_peer(const uint8_t *mac)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);

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
            xSemaphoreGive(state_mutex);
            ESP_LOGI(TAG, "Removed peer: %02x:%02x:%02x:%02x:%02x:%02x", mac[0],
                     mac[1], mac[2], mac[3], mac[4], mac[5]);
            return;
        }
    }

    xSemaphoreGive(state_mutex);
}
