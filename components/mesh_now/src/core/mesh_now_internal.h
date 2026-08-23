#ifndef MESH_NOW_INTERNAL_H
#define MESH_NOW_INTERNAL_H

#include "mesh_now.h"
#include "message_queue.h"
#include <esp_now.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifndef CONFIG_MESH_NOW_BEACON_INTERVAL_MS
#define CONFIG_MESH_NOW_BEACON_INTERVAL_MS 5000
#endif
#ifndef CONFIG_MESH_NOW_RETRANSMIT_TIMEOUT_MS
#define CONFIG_MESH_NOW_RETRANSMIT_TIMEOUT_MS 2000
#endif
#ifndef CONFIG_MESH_NOW_MAX_RETRIES
#define CONFIG_MESH_NOW_MAX_RETRIES 3
#endif
#ifndef CONFIG_MESH_NOW_MAX_PENDING_MESSAGES
#define CONFIG_MESH_NOW_MAX_PENDING_MESSAGES 16
#endif
#ifndef CONFIG_MESH_NOW_MAX_SEEN_MESSAGE_IDS
#define CONFIG_MESH_NOW_MAX_SEEN_MESSAGE_IDS 128
#endif
#ifndef CONFIG_MESH_NOW_DEFAULT_ROUTE_TTL
#define CONFIG_MESH_NOW_DEFAULT_ROUTE_TTL 3
#endif
#ifndef CONFIG_MESH_NOW_PEER_EXPIRY_SEC
#define CONFIG_MESH_NOW_PEER_EXPIRY_SEC 30
#endif

#define BEACON_INTERVAL_MS CONFIG_MESH_NOW_BEACON_INTERVAL_MS
#define RETRANSMIT_TIMEOUT_MS CONFIG_MESH_NOW_RETRANSMIT_TIMEOUT_MS
#define MAX_RETRIES CONFIG_MESH_NOW_MAX_RETRIES
#define MAX_PENDING_MESSAGES CONFIG_MESH_NOW_MAX_PENDING_MESSAGES
#define MAX_SEEN_MESSAGE_IDS CONFIG_MESH_NOW_MAX_SEEN_MESSAGE_IDS
#define MAX_ENCRYPTION_KEY 32
#define PEER_EXPIRY_US ((int64_t)CONFIG_MESH_NOW_PEER_EXPIRY_SEC * 1000000LL)
#define AES_GCM_TAG_LEN 16
#define AES_GCM_NONCE_LEN 12
#define WIRE_BUF_SIZE 256

typedef struct {
    bool active;
    uint32_t message_id;
    uint8_t flags;
    uint8_t wire_buf[WIRE_BUF_SIZE];
    size_t wire_len;
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];
    int retries;
    int64_t last_send_time_ms;
} pending_message_t;

extern mesh_peer_t peers[MAX_PEERS];
extern int peer_count;
extern uint8_t broadcast_mac[ESP_NOW_ETH_ALEN];
extern TaskHandle_t beacon_task_handle;
extern TaskHandle_t retransmit_task_handle;
extern mesh_now_receive_callback_t receive_callback;
extern bool encryption_enabled;
extern uint8_t encryption_key[MAX_ENCRYPTION_KEY];
extern size_t encryption_key_len;
extern uint32_t next_message_id;
extern uint8_t local_group_id;
extern char local_node_name[MESH_NOW_NODE_NAME_MAX + 1];
extern pending_message_t pending_messages[MAX_PENDING_MESSAGES];
extern uint32_t seen_message_ids[MAX_SEEN_MESSAGE_IDS];
extern int seen_message_count;
extern SemaphoreHandle_t state_mutex;
extern int64_t time_offset_us;

uint32_t mesh_now_generate_message_id(void);
bool mesh_now_is_message_seen(uint32_t message_id);
void mesh_now_mark_message_seen(uint32_t message_id);
int mesh_now_allocate_pending(void);
int mesh_now_find_pending(uint32_t message_id);
void mesh_now_release_pending(int index);

void mesh_now_build_nonce(uint32_t message_id, const uint8_t *sender_mac,
                          uint8_t nonce[AES_GCM_NONCE_LEN]);
void mesh_now_sync_time(uint32_t remote_timestamp);
uint32_t mesh_now_get_network_time_ms(void);
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
size_t mesh_now_build_aad(const mesh_message_t *msg, uint8_t *aad);

esp_err_t mesh_now_prepare_wire(const mesh_message_t *msg,
                                 uint8_t *wire, size_t *wire_len,
                                 bool do_encrypt);
bool mesh_now_decode_wire(const uint8_t *data, size_t len,
                           mesh_message_t *msg);

void mesh_now_add_peer(const uint8_t *mac);
void mesh_now_remove_peer(const uint8_t *mac);

void mesh_now_route_message(mesh_message_t *msg);
void mesh_now_send_ack(const mesh_message_t *received_msg);
void mesh_now_handle_message(const mesh_message_t *mesh_msg);

void mesh_now_register_callbacks(void);
esp_err_t mesh_now_start_tasks(void);

#endif
