#ifndef MESH_NOW_H
#define MESH_NOW_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <esp_err.h>
#include <esp_now.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_MESH_MESSAGE_LEN 128
#ifndef CONFIG_MESH_NOW_DEFAULT_ROUTE_TTL
#define CONFIG_MESH_NOW_DEFAULT_ROUTE_TTL 3
#endif
#define DEFAULT_ROUTE_TTL      CONFIG_MESH_NOW_DEFAULT_ROUTE_TTL
#define MESH_NOW_NODE_NAME_MAX 16

#define MSG_FLAG_REQUIRES_ACK  0x01
#define MSG_FLAG_ENCRYPTED     0x02
#define MSG_FLAG_HAS_NODE_NAME 0x04

#define MESH_NOW_MAGIC_0 0x4d
#define MESH_NOW_MAGIC_1 0x4e

// Wire format version. Decoders reject any other version.
#define MESH_NOW_WIRE_VERSION 1
#define MESH_NOW_HEADER_LEN   31

typedef struct {
    uint8_t type;
    uint8_t flags;
    uint8_t group_id;
    uint8_t hop_count;
    uint32_t message_id;
    uint32_t reply_to;
    uint8_t sender_mac[ESP_NOW_ETH_ALEN];
    uint8_t target_mac[ESP_NOW_ETH_ALEN];
    uint32_t timestamp;
    char message[MAX_MESH_MESSAGE_LEN];
    char node_name[MESH_NOW_NODE_NAME_MAX + 1];
} mesh_message_t;

#define MSG_TYPE_BEACON   0
#define MSG_TYPE_CHAT     1
#define MSG_TYPE_DIRECT   2
#define MSG_TYPE_ACK      3
#define MSG_TYPE_GROUP    4
#define MSG_TYPE_PRESENCE 5
#define MSG_TYPE_TYPING   6

typedef struct {
    uint8_t peer_addr[ESP_NOW_ETH_ALEN];
    bool active;
    int64_t last_seen;
    char node_name[MESH_NOW_NODE_NAME_MAX + 1];
} mesh_peer_t;

#define MAX_PEERS     20
#define BROADCAST_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

typedef void (*mesh_now_receive_callback_t)(const mesh_message_t *message);

esp_err_t mesh_now_init(void);
esp_err_t mesh_now_deinit(void);
void mesh_now_add_peer(const uint8_t *mac);
void mesh_now_remove_peer(const uint8_t *mac);
void mesh_now_set_receive_callback(mesh_now_receive_callback_t callback);
esp_err_t mesh_now_send_broadcast(const char *message);
esp_err_t mesh_now_send_message(const char *message);
esp_err_t mesh_now_send_direct(const uint8_t *target_mac, const char *message);
esp_err_t mesh_now_send_group(uint8_t group_id, const char *message);
esp_err_t mesh_now_send_presence(const char *status);
esp_err_t mesh_now_send_typing(const uint8_t *target_mac, bool typing);
esp_err_t mesh_now_set_group(uint8_t group_id);
esp_err_t mesh_now_set_encryption_key(const uint8_t *key, size_t len);
esp_err_t mesh_now_set_name(const char *name);
const char *mesh_now_get_name(void);
esp_err_t mesh_now_announce_name(void);
int mesh_now_get_peer_count(void);
mesh_peer_t *mesh_now_get_peers(void);
bool mesh_now_is_encrypted(void);
uint8_t mesh_now_get_group_id(void);
bool mesh_now_peer_is_online(const mesh_peer_t *peer);

size_t mesh_now_encode(const mesh_message_t *msg, uint8_t *out,
                       size_t out_size);
bool mesh_now_decode(const uint8_t *data, size_t len, mesh_message_t *msg);

#ifdef __cplusplus
}
#endif

#endif // MESH_NOW_H
