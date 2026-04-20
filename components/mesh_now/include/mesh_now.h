#ifndef MESH_NOW_H
#define MESH_NOW_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_now.h>

#ifdef __cplusplus
extern "C" {
#endif

// Message structure for ESP-NOW
typedef struct {
    uint8_t type;  // 0 = beacon, 1 = chat message, 2 = direct message
    char message[256];
    uint8_t sender_mac[ESP_NOW_ETH_ALEN];
    uint8_t target_mac[ESP_NOW_ETH_ALEN];
    uint32_t timestamp;
} mesh_message_t;

// Message types
#define MSG_TYPE_BEACON 0
#define MSG_TYPE_CHAT   1
#define MSG_TYPE_DIRECT 2

// Peer management
typedef struct {
    uint8_t peer_addr[ESP_NOW_ETH_ALEN];
    bool active;
} mesh_peer_t;

#define MAX_PEERS 20
#define BROADCAST_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

// Callback type for received mesh messages
typedef void (*mesh_now_receive_callback_t)(const mesh_message_t *message);

// Function declarations
esp_err_t mesh_now_init(void);
esp_err_t mesh_now_deinit(void);
void mesh_now_add_peer(const uint8_t *mac);
void mesh_now_remove_peer(const uint8_t *mac);
void mesh_now_set_receive_callback(mesh_now_receive_callback_t callback);
esp_err_t mesh_now_send_message(const char *message);
esp_err_t mesh_now_send_direct(const uint8_t *target_mac, const char *message);
int mesh_now_get_peer_count(void);
mesh_peer_t* mesh_now_get_peers(void);
#ifdef __cplusplus
}
#endif

#endif // MESH_NOW_H