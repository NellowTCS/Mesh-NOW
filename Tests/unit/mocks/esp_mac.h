#ifndef MOCK_ESP_MAC_H
#define MOCK_ESP_MAC_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_MAC_WIFI_STA,
    ESP_MAC_WIFI_SOFTAP,
    ESP_MAC_BT,
    ESP_MAC_ETH,
    ESP_MAC_BASE,
    ESP_MAC_IEEE802154,
} esp_mac_type_t;

esp_err_t esp_read_mac(uint8_t mac[6], esp_mac_type_t type);

#ifdef __cplusplus
}
#endif

#endif