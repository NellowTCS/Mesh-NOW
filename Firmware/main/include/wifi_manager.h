#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <esp_err.h>

// ESP-NOW requires every mesh node on the same WiFi channel.
#ifndef CONFIG_MESH_NOW_WIFI_CHANNEL
#define CONFIG_MESH_NOW_WIFI_CHANNEL 1
#endif
#define WIFI_CHANNEL CONFIG_MESH_NOW_WIFI_CHANNEL

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_deinit(void);

#endif // WIFI_MANAGER_H