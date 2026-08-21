#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdint.h>
#include <esp_err.h>

// WiFi configuration, SSID will be generated dynamically
#define WIFI_PASS "password"
#define WIFI_CHANNEL 1
#define WIFI_SSID_BASE "MESH-NOW"

// Function declarations
esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_deinit(void);
void wifi_manager_register_peer_callbacks(void (*add_peer_cb)(const uint8_t *mac),
                                         void (*remove_peer_cb)(const uint8_t *mac));

#endif // WIFI_MANAGER_H