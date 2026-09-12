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

// Max message string length, excluding the null terminator.
#define MAX_MESH_MESSAGE_LEN 128

// Default hop limit for outgoing data frames.
#ifndef CONFIG_MESH_NOW_DEFAULT_ROUTE_TTL
#define CONFIG_MESH_NOW_DEFAULT_ROUTE_TTL 3
#endif
#define DEFAULT_ROUTE_TTL CONFIG_MESH_NOW_DEFAULT_ROUTE_TTL

// Max node name length, excluding the null terminator.
#define MESH_NOW_NODE_NAME_MAX 16

// How many one-hop neighbors a beacon may advertise.
#ifndef CONFIG_MESH_NOW_MAX_BEACON_NEIGHBORS
#define CONFIG_MESH_NOW_MAX_BEACON_NEIGHBORS 8
#endif

// Cap on the physical (one-hop) ESP-NOW peer table.
#define MAX_PEERS 20

// Wire magic bytes ("MN").
#define MESH_NOW_MAGIC_0 0x4d
#define MESH_NOW_MAGIC_1 0x4e

// Wire format version. Decoders reject any other version.
#define MESH_NOW_WIRE_VERSION 1
#define MESH_NOW_HEADER_LEN   32

// Broadcast MAC address for ESP-NOW.
#define BROADCAST_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

// Message types.
#define MSG_TYPE_BEACON        0
#define MSG_TYPE_CHAT          1
#define MSG_TYPE_DIRECT        2
#define MSG_TYPE_ACK           3
#define MSG_TYPE_GROUP         4
#define MSG_TYPE_PRESENCE      5
#define MSG_TYPE_TYPING        6
#define MSG_TYPE_ROUTE_REQUEST 7
#define MSG_TYPE_ROUTE_REPLY   8
#define MSG_TYPE_ROUTE_ERROR   9

// Message flags.
#define MSG_FLAG_REQUIRES_ACK  0x01
#define MSG_FLAG_ENCRYPTED     0x02
#define MSG_FLAG_HAS_NODE_NAME 0x04

// One unit of communication in the mesh. Fields map 1:1 onto the wire header.
typedef struct {
    uint8_t type;
    uint8_t flags;
    uint8_t group_id;
    uint8_t hop_limit;
    uint8_t hop_count;
    uint32_t message_id;
    uint32_t reply_to;
    uint8_t sender_mac[ESP_NOW_ETH_ALEN];
    uint8_t target_mac[ESP_NOW_ETH_ALEN];
    uint32_t timestamp;
    char message[MAX_MESH_MESSAGE_LEN];
    char node_name[MESH_NOW_NODE_NAME_MAX + 1];
    uint8_t neighbor_count;
    uint8_t neighbor_macs[CONFIG_MESH_NOW_MAX_BEACON_NEIGHBORS]
                         [ESP_NOW_ETH_ALEN];
    char neighbor_names[CONFIG_MESH_NOW_MAX_BEACON_NEIGHBORS]
                       [MESH_NOW_NODE_NAME_MAX + 1];
} mesh_message_t;

// A one-hop neighbor, discovered via ESP-NOW beacons.
typedef struct {
    uint8_t peer_addr[ESP_NOW_ETH_ALEN];
    bool active;
    int64_t last_seen;
    char node_name[MESH_NOW_NODE_NAME_MAX + 1];
} mesh_peer_t;

// Route to a virtual peer: reachable via next_hop, not a direct neighbor.
typedef struct {
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];
    uint8_t next_hop[ESP_NOW_ETH_ALEN];
    uint8_t hop_count;
    uint32_t dest_seq;
    int64_t last_used_us;
    bool active;
    bool pinned;
    char node_name[MESH_NOW_NODE_NAME_MAX + 1];
} mesh_route_t;

// Called for every data message this node receives or relays.
typedef void (*mesh_now_receive_callback_t)(const mesh_message_t *message);
// Called when a route could not be found for dest_mac.
typedef void (*mesh_now_route_failure_callback_t)(const uint8_t *dest_mac);

// Register mac as a one-hop ESP-NOW peer on the mesh channel.
void mesh_now_add_peer(const uint8_t *mac);
// Unregister mac and drop it from the peer table.
void mesh_now_remove_peer(const uint8_t *mac);
// Install the data handler; overrides the internal message queue.
void mesh_now_set_receive_callback(mesh_now_receive_callback_t callback);
// Install the handler fired when a route could not be found.
void mesh_now_set_route_failure_callback(mesh_now_route_failure_callback_t cb);

// Local node name; empty until mesh_now_set_name().
const char *mesh_now_get_name(void);

// True after mesh_now_set_encryption_key().
bool mesh_now_is_encrypted(void);
// True if peer was contacted within the peer expiry window.
bool mesh_now_peer_is_online(const mesh_peer_t *peer);
// Copy the route to dest_mac into out, if one exists.
bool mesh_now_get_route(const uint8_t *dest_mac, mesh_route_t *out);
// Parse an mpack payload into a mesh_message_t.
bool mesh_now_decode(const uint8_t *data, size_t len, mesh_message_t *msg);

// Number of active physical peers.
int mesh_now_get_peer_count(void);
// Copy up to max_out peers into out; returns the count copied.
int mesh_now_snapshot_peers(mesh_peer_t *out, size_t max_out);
// Number of active route entries.
int mesh_now_get_route_count(void);
// Copy up to max_out routes into out; returns the count copied.
int mesh_now_snapshot_routes(mesh_route_t *out, size_t max_out);

// Current group id (0 = not in a group).
uint8_t mesh_now_get_group_id(void);

// Serialize a mesh_message_t to mpack; returns bytes written.
size_t mesh_now_encode(const mesh_message_t *msg, uint8_t *out,
                       size_t out_size);

// Direct access to the peer table; the serial API snapshots from here.
mesh_peer_t *mesh_now_get_peers(void);

// Initialize ESP-NOW, the background tasks, and the broadcast peer.
esp_err_t mesh_now_init(void);
// Tear down tasks, callbacks, peers, and ESP-NOW.
esp_err_t mesh_now_deinit(void);
// Send a chat message to the whole mesh.
esp_err_t mesh_now_send_broadcast(const char *message);
// Alias for mesh_now_send_broadcast().
esp_err_t mesh_now_send_message(const char *message);
// Send an ACKed DM to target_mac; routed if it is not a neighbor.
esp_err_t mesh_now_send_direct(const uint8_t *target_mac, const char *message);
// Send a chat message to group_id.
esp_err_t mesh_now_send_group(uint8_t group_id, const char *message);
// Broadcast a presence status string.
esp_err_t mesh_now_send_presence(const char *status);
// Send a typing indicator to target_mac.
esp_err_t mesh_now_send_typing(const uint8_t *target_mac, bool typing);
// Join or leave a group; 0 leaves all groups.
esp_err_t mesh_now_set_group(uint8_t group_id);
// Enable AES-128-GCM with an exactly 16-byte key.
esp_err_t mesh_now_set_encryption_key(const uint8_t *key, size_t len);
// Set the local node name (truncated to MESH_NOW_NODE_NAME_MAX).
esp_err_t mesh_now_set_name(const char *name);
// Push the current name out with an immediate beacon.
esp_err_t mesh_now_announce_name(void);
// Pin a fixed route to dest_mac via proxy_mac: bypasses discovery, never
// expires.
esp_err_t mesh_now_pin_route(const uint8_t *dest_mac, const uint8_t *proxy_mac);
// Remove a pinned route; fails if none exists.
esp_err_t mesh_now_unpin_route(const uint8_t *dest_mac);

#ifdef __cplusplus
}
#endif

#endif // MESH_NOW_H