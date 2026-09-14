#ifndef MOCK_ESP_NOW_H
#define MOCK_ESP_NOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_NOW_ETH_ALEN     6
#define ESP_NOW_MAX_DATA_LEN 250

typedef struct {
    uint8_t peer_addr[ESP_NOW_ETH_ALEN];
    uint8_t lmk[16];
    uint8_t channel;
    uint8_t ifidx;
    bool encrypt;
    bool priv;
} esp_now_peer_info_t;

esp_err_t esp_now_init(void);
esp_err_t esp_now_deinit(void);
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer);
esp_err_t esp_now_del_peer(const uint8_t *mac_addr);
esp_err_t esp_now_unregister_send_cb(void);
esp_err_t esp_now_unregister_recv_cb(void);
bool esp_now_is_peer_exist(const uint8_t *mac_addr);
esp_err_t esp_now_send(const uint8_t *mac_addr, const uint8_t *data,
                       size_t data_len);

#ifdef __cplusplus
}
#endif

#endif