#ifndef MESH_NOW_H
#define MESH_NOW_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_now.h>

// Message structure for ESP-NOW
typedef struct {
    char message[256];
    uint8_t sender_mac[ESP_NOW_ETH_ALEN];
    uint32_t timestamp;
} mesh_message_t;

// Peer management
typedef struct {
    uint8_t peer_addr[ESP_NOW_ETH_ALEN];
    bool active;
} mesh_peer_t;

#define MAX_PEERS 20
#define BROADCAST_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

// Function declarations
esp_err_t mesh_now_init(void);
esp_err_t mesh_now_deinit(void);
void mesh_now_add_peer(const uint8_t *mac);
void mesh_now_remove_peer(const uint8_t *mac);
esp_err_t mesh_now_send_message(const char *message);
int mesh_now_get_peer_count(void);
mesh_peer_t* mesh_now_get_peers(void);

#endif // MESH_NOW_H