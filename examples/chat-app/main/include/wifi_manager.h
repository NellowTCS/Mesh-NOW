#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <esp_err.h>

// ESP-NOW requires every mesh node on the same WiFi channel.
#define WIFI_CHANNEL 1

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_deinit(void);

#endif // WIFI_MANAGER_H