#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <string.h>

#define TAG "MESH_NOW"

// Index of a peer by MAC within the active region [0, peer_count),
// or -1 if not present.
static int find_peer(const uint8_t *mac)
{
    for (int i = 0; i < peer_count; i++) {
        if (memcmp(peers[i].peer_addr, mac, ESP_NOW_ETH_ALEN) == 0) {
            return i;
        }
    }
    return -1;
}

// Register a peer with ESP-NOW on the shared mesh channel. Tolerates an
// already-registered peer.
static esp_err_t add_peer_espnow(const uint8_t *mac)
{
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
    peer.channel = WIFI_CHANNEL;
    peer.encrypt = false;

    esp_err_t rc = esp_now_add_peer(&peer);
    return (rc == ESP_OK || rc == ESP_ERR_ESPNOW_EXIST) ? ESP_OK : rc;
}

void mesh_now_add_peer(const uint8_t *mac)
{
    uint8_t my_mac[ESP_NOW_ETH_ALEN];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    if (memcmp(my_mac, mac, ESP_NOW_ETH_ALEN) == 0) {
        return;
    }

    xSemaphoreTake(state_mutex, portMAX_DELAY);

    int idx = find_peer(mac);
    if (idx >= 0) {
        // Peer expired earlier but is back on air: reactivate and refresh.
        peers[idx].active = true;
        peers[idx].last_seen = esp_timer_get_time();
        if (!esp_now_is_peer_exist(mac)) {
            add_peer_espnow(mac);
        }
        xSemaphoreGive(state_mutex);
        return;
    }

    if (peer_count >= MAX_PEERS) {
        ESP_LOGW(TAG,
                 "Max peers reached, cannot add: %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        xSemaphoreGive(state_mutex);
        return;
    }

    esp_err_t rc = add_peer_espnow(mac);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "esp_now_add_peer failed: %s", esp_err_to_name(rc));
        xSemaphoreGive(state_mutex);
        return;
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

// True if mac is currently a one-hop ESP-NOW neighbor on the mesh channel.
bool mesh_now_peer_is_direct(const uint8_t *mac)
{
    bool found = false;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    for (int i = 0; i < peer_count; i++) {
        if (memcmp(peers[i].peer_addr, mac, ESP_NOW_ETH_ALEN) == 0) {
            found = true;
            break;
        }
    }
    xSemaphoreGive(state_mutex);
    return found;
}

// Mark peers idle past PEER_EXPIRY_US, then compact so peer_count tracks
// active peers.
void mesh_now_expire_peers(int64_t now_us)
{
    uint8_t expired_macs[MAX_PEERS][ESP_NOW_ETH_ALEN];
    int expired_count = 0;

    xSemaphoreTake(state_mutex, portMAX_DELAY);

    for (int i = 0; i < peer_count; i++) {
        if (peers[i].active && (now_us - peers[i].last_seen) > PEER_EXPIRY_US) {
            ESP_LOGI(TAG, "Peer expired: %02x:%02x:%02x:%02x:%02x:%02x",
                     peers[i].peer_addr[0], peers[i].peer_addr[1],
                     peers[i].peer_addr[2], peers[i].peer_addr[3],
                     peers[i].peer_addr[4], peers[i].peer_addr[5]);
            peers[i].active = false;
            // Free the ESP-NOW registration slot; the next beacon from this
            // node re-registers it.
            esp_err_t rc = esp_now_del_peer(peers[i].peer_addr);
            if (rc != ESP_OK && rc != ESP_ERR_ESPNOW_NOT_FOUND) {
                ESP_LOGW(TAG, "esp_now_del_peer failed: %s",
                         esp_err_to_name(rc));
            }
            if (expired_count < MAX_PEERS) {
                memcpy(expired_macs[expired_count], peers[i].peer_addr,
                       ESP_NOW_ETH_ALEN);
                expired_count++;
            }
        }
    }

    // Compact: move active entries to the front, preserving order.
    int write = 0;
    for (int i = 0; i < peer_count; i++) {
        if (peers[i].active) {
            if (write != i) {
                peers[write] = peers[i];
            }
            write++;
        }
    }
    peer_count = write;

    xSemaphoreGive(state_mutex);

    for (int i = 0; i < expired_count; i++) {
        if (mesh_now_invalidate_routes_through(expired_macs[i]) > 0) {
            mesh_now_send_rerr();
        }
    }
}
