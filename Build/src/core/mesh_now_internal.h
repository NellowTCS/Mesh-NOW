#ifndef MESH_NOW_INTERNAL_H
#define MESH_NOW_INTERNAL_H

#include "mesh_now.h"
#include "message_queue.h"
#include "mesh_now_pool.h"
#include "mesh_now_config.h"
#include <esp_now.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Internal state for the Mesh-NOW library. This is not part of the public API
// and is subject to change.

#define BEACON_INTERVAL_MS    CONFIG_MESH_NOW_BEACON_INTERVAL_MS
#define RETRANSMIT_TIMEOUT_MS CONFIG_MESH_NOW_RETRANSMIT_TIMEOUT_MS
#define MAX_RETRIES           CONFIG_MESH_NOW_MAX_RETRIES
#define MAX_PENDING_MESSAGES  CONFIG_MESH_NOW_MAX_PENDING_MESSAGES
#define MAX_SEEN_MESSAGE_IDS  CONFIG_MESH_NOW_MAX_SEEN_MESSAGE_IDS
#define MAX_ROUTES            CONFIG_MESH_NOW_MAX_ROUTES
#define PEER_EXPIRY_US ((int64_t)CONFIG_MESH_NOW_PEER_EXPIRY_SEC * 1000000LL)
#define ROUTE_LIFETIME_US                                                      \
    ((int64_t)CONFIG_MESH_NOW_ROUTE_LIFETIME_SEC * 1000000LL)
#define ROUTE_REQ_TIMEOUT_US                                                   \
    ((int64_t)CONFIG_MESH_NOW_ROUTE_REQ_TIMEOUT_MS * 1000LL)
#define ROUTE_REQ_TTL         CONFIG_MESH_NOW_ROUTE_REQ_TTL
#define MAX_ROUTE_REQ_RETRIES CONFIG_MESH_NOW_MAX_ROUTE_REQ_RETRIES
#define MAX_ROUTE_REQUESTS    CONFIG_MESH_NOW_MAX_ROUTE_REQUESTS
#define MAX_RREQ_CACHE        CONFIG_MESH_NOW_RREQ_CACHE_SIZE
#define WIFI_CHANNEL          CONFIG_MESH_NOW_WIFI_CHANNEL
#define MAX_BEACON_NEIGHBORS  CONFIG_MESH_NOW_MAX_BEACON_NEIGHBORS
#define MAX_ENCRYPTION_KEY    AES_GCM_KEY_LEN
#define AES_GCM_TAG_LEN       16
#define AES_GCM_NONCE_LEN     12
#define AES_GCM_KEY_LEN       16
#define WIRE_BUF_SIZE         CONFIG_MESH_NOW_WIRE_BUF_SIZE
#define PENDING_WIRE_BUF      ESP_NOW_MAX_DATA_LEN

// A pending outbound message awaiting ACK (or a route for a buffered DM).
typedef struct {
    uint32_t message_id;
    uint8_t flags;
    uint8_t wire_buf[PENDING_WIRE_BUF];
    size_t wire_len;
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];
    uint8_t remote_dest[ESP_NOW_ETH_ALEN];
    bool active;
    bool route_wait;
    int retries;
    int64_t last_send_time_ms;
} pending_message_t;

// Dedup cache for forwarded control frames (RREQ/RREP/RERR).
typedef struct {
    bool active;
    uint8_t origin_mac[ESP_NOW_ETH_ALEN];
    uint32_t message_id;
    int64_t expires_us;
} rreq_cache_entry_t;

// A single in-flight route discovery for one destination.
typedef struct {
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];
    uint32_t rreq_seq;
    uint32_t last_dest_seq;
    uint8_t attempts;
    int64_t expires_us;
    bool active;
} route_request_t;

// Parsed form of the fixed 32-byte wire header. Serialization lives in
// mesh_now_codec.c; every frame type shares it.
typedef struct {
    uint8_t flags;
    uint8_t type;
    uint8_t group_id;
    uint8_t hop_limit;
    uint8_t hop_count;
    uint32_t message_id;
    uint32_t reply_to;
    uint32_t timestamp;
    uint8_t sender_mac[ESP_NOW_ETH_ALEN];
    uint8_t target_mac[ESP_NOW_ETH_ALEN];
} mesh_wire_header_t;

// Shared glue-table state.
extern mesh_now_pool_t peer_pool;
extern mesh_now_pool_t route_pool;
extern mesh_now_pool_t rreq_cache_pool;
extern mesh_now_pool_t route_request_pool;
extern mesh_now_pool_t pending_pool;
extern mesh_now_pool_t seen_id_pool;

// Active-entry counts, mirrored from pool.used_count for callers that want an
// int. Note that these are only accurate when entries are freed from their pool
// on removal/expiry (which every table does!)
extern int peer_count;
extern int route_count;
extern int route_request_count;

extern uint8_t broadcast_mac[ESP_NOW_ETH_ALEN];
extern TaskHandle_t beacon_task_handle;
extern TaskHandle_t retransmit_task_handle;
extern mesh_now_receive_callback_t receive_callback;
extern mesh_now_route_failure_callback_t route_failure_callback;
extern bool encryption_enabled;
extern uint8_t encryption_key[MAX_ENCRYPTION_KEY];
extern size_t encryption_key_len;
extern uint32_t next_message_id;
extern uint8_t local_group_id;
extern char local_node_name[MESH_NOW_NODE_NAME_MAX + 1];
extern SemaphoreHandle_t state_mutex;
extern int64_t time_offset_us;

// mpack payload (de)serialization.
bool mesh_now_decode(const uint8_t *data, size_t len, mesh_message_t *msg);
size_t mesh_now_encode(const mesh_message_t *msg, uint8_t *out,
                       size_t out_size);

// net: physical peers.
void mesh_now_add_peer(const uint8_t *mac);
void mesh_now_remove_peer(const uint8_t *mac);
void mesh_now_expire_peers(int64_t now_us);
bool mesh_now_peer_is_direct(const uint8_t *mac);

// net: virtual-peer route table.
void mesh_now_add_route(const uint8_t *dest_mac, const uint8_t *next_hop,
                        uint8_t hop_count, uint32_t dest_seq);
void mesh_now_add_virtual_peer(const uint8_t *dest_mac, const uint8_t *via_mac,
                               const char *name, uint8_t hop_count);
void mesh_now_route_set_name(const uint8_t *dest_mac, const char *name);
void mesh_now_expire_routes(int64_t now_us);
int mesh_now_invalidate_routes_through(const uint8_t *next_hop);

// net: route discovery (RREQ/RREP/RERR protocol).
esp_err_t mesh_now_request_route(const uint8_t *target);
int mesh_now_route_request_attempts(const uint8_t *target);
void mesh_now_expire_route_requests(int64_t now_us);
bool mesh_now_rreq_cache_hit(const uint8_t *origin_mac, uint32_t message_id);
void mesh_now_rreq_cache_record(const uint8_t *origin_mac, uint32_t message_id);
void mesh_now_handle_rreq(const mesh_message_t *msg, const uint8_t *src_addr);
void mesh_now_handle_rrep(const mesh_message_t *msg, const uint8_t *src_addr);
void mesh_now_send_rerr(void);
void mesh_now_handle_rerr(const mesh_message_t *msg);

// net: shared wire transmit/relay + logging helpers.
const char *mesh_now_mac_str(const uint8_t *mac);
bool mesh_now_can_relay(const mesh_message_t *msg);
esp_err_t mesh_now_send_frame(const mesh_message_t *msg, const uint8_t *dest,
                              bool do_encrypt);
esp_err_t mesh_now_relay_unicast(const mesh_message_t *msg,
                                 const uint8_t *next_hop, bool do_encrypt);
esp_err_t mesh_now_relay_broadcast(const mesh_message_t *msg, bool do_encrypt);

// net: transmit path (public send API lives here too).
void mesh_now_send_ack(const mesh_message_t *received_msg,
                       const uint8_t *prev_hop);

// net: background tasks.
esp_err_t mesh_now_start_tasks(void);
esp_err_t mesh_now_announce_name(void);

// net: retransmission state machine.
void mesh_now_retransmit_task_fn(void *pvParameters);

// core: lifecycle, time, identifier, and per-node settings state.
void mesh_now_register_callbacks(void);
void mesh_now_handle_message(const mesh_message_t *mesh_msg);
uint32_t mesh_now_get_network_time_ms(void);
uint32_t mesh_now_generate_message_id(void);
void mesh_now_sync_time(uint32_t remote_timestamp);
bool mesh_now_is_message_seen(uint32_t message_id);
void mesh_now_mark_message_seen(uint32_t message_id);

// core: pending-message buffer.
int mesh_now_add_pending(const uint8_t *dest_mac, const uint8_t *remote_dest,
                         const uint8_t *wire, size_t wire_len,
                         uint32_t message_id, uint8_t flags, bool route_wait);
// Find a pending block by message ID. Returns 0 if found, -1 otherwise.
int mesh_now_find_pending(uint32_t message_id);
// Find a pending block by message ID. Returns the block pointer or NULL
// Caller must hold state_mutex!
pending_message_t *mesh_now_find_pending_locked(uint32_t message_id);
// Release a pending block by message ID (frees it back to the pool).
void mesh_now_release_pending(uint32_t message_id);
void mesh_now_flush_route_wait(const uint8_t *target);

// codec: wire framing.
bool mesh_now_decode_wire(const uint8_t *data, size_t len, mesh_message_t *msg);
esp_err_t mesh_now_prepare_wire(const mesh_message_t *msg, uint8_t *wire,
                                size_t *wire_len, bool do_encrypt);
bool mesh_now_is_control_type(uint8_t type);
bool mesh_now_carries_node_name(uint8_t type);
size_t mesh_now_build_aad(const mesh_message_t *msg, uint8_t *aad);

// crypto.
void mesh_now_build_nonce(uint32_t message_id, const uint8_t *sender_mac,
                          uint8_t nonce[AES_GCM_NONCE_LEN]);
esp_err_t mesh_now_aes_gcm_encrypt(const uint8_t *plaintext, size_t pt_len,
                                   const uint8_t *aad, size_t aad_len,
                                   const uint8_t key[16],
                                   const uint8_t nonce[AES_GCM_NONCE_LEN],
                                   uint8_t *ciphertext,
                                   uint8_t tag[AES_GCM_TAG_LEN]);
esp_err_t mesh_now_aes_gcm_decrypt(const uint8_t *ciphertext, size_t ct_len,
                                   const uint8_t *aad, size_t aad_len,
                                   const uint8_t key[16],
                                   const uint8_t nonce[AES_GCM_NONCE_LEN],
                                   const uint8_t tag[AES_GCM_TAG_LEN],
                                   uint8_t *plaintext);

#endif