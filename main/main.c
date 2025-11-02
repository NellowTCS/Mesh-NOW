#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

// Modular includes
#include "mesh_now.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "message_queue.h"

#define TAG "MESH_NOW_MAIN"

// Global message send function for web server
esp_err_t send_message_callback(const char *message) {
    return mesh_now_send_message(message);
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting Mesh-NOW ESP32 Chat Application");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize message queue
    ESP_ERROR_CHECK(message_queue_init());

    // Initialize WiFi
    ESP_ERROR_CHECK(wifi_manager_init());

    // Register peer callbacks with WiFi manager
    wifi_manager_register_peer_callbacks(mesh_now_add_peer, mesh_now_remove_peer);

    // Initialize ESP-NOW mesh
    ESP_ERROR_CHECK(mesh_now_init());

    // Initialize web server
    ESP_ERROR_CHECK(web_server_init(message_queue_get_handle()));

    // Set up message sending callback for web server
    web_server_set_send_callback(mesh_now_send_message);

    // Print device information
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "Device MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "Mesh-NOW initialized successfully!");
    ESP_LOGI(TAG, "Connect to WiFi AP 'MESH-NOW' with password 'password'");
    ESP_LOGI(TAG, "Open http://192.168.4.1 in your browser");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Log peer count periodically
        int peer_count = mesh_now_get_peer_count();
        if (peer_count > 0) {
            ESP_LOGI(TAG, "Active mesh peers: %d", peer_count);
        }
    }
}
