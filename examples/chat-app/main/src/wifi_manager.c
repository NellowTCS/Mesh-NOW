#include "wifi_manager.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_mac.h>
#include <nvs_flash.h>
#include <string.h>

#define TAG "WIFI_MGR"

#define TAG "WIFI_MGR"

static void (*add_peer_callback)(const uint8_t *mac) = NULL;
static void (*remove_peer_callback)(const uint8_t *mac) = NULL;

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station connected: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5]);
        // Note: WiFi clients (phones/laptops) connecting to our AP are NOT mesh nodes
        // ESP-NOW peer discovery happens via broadcast beacons, not WiFi connections
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station disconnected: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5]);
    }
}

esp_err_t wifi_manager_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();  // Create STA interface for ESP-NOW

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    // Generate unique SSID using last 4 bytes of MAC address
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    
    char unique_ssid[32];
    snprintf(unique_ssid, sizeof(unique_ssid), "%s-%02X%02X%02X%02X", 
             WIFI_SSID_BASE, mac[2], mac[3], mac[4], mac[5]);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(unique_ssid),
            .password = WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .channel = WIFI_CHANNEL,
        },
    };
    
    // Copy SSID to config (ESP-IDF requires this approach)
    memcpy(wifi_config.ap.ssid, unique_ssid, sizeof(wifi_config.ap.ssid));

    // Use APSTA mode - Required for ESP-NOW to work properly!
    // AP mode provides web interface, STA mode enables ESP-NOW mesh communication
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE)); // ESPNOW_CHANNEL

    ESP_LOGI(TAG, "WiFi APSTA started. SSID: %s, Password: %s, Channel: %d",
             unique_ssid, WIFI_PASS, WIFI_CHANNEL);
    ESP_LOGI(TAG, "ESP-NOW will use STA interface for mesh communication");

    return ESP_OK;
}

esp_err_t wifi_manager_deinit(void) {
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_deinit();
    return ESP_OK;
}

void wifi_manager_register_peer_callbacks(void (*add_peer_cb)(const uint8_t *mac),
                                         void (*remove_peer_cb)(const uint8_t *mac)) {
    add_peer_callback = add_peer_cb;
    remove_peer_callback = remove_peer_cb;
}