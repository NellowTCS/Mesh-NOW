#include "wifi_manager.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>

#define TAG "WIFI_MGR"

static void (*add_peer_callback)(const uint8_t *mac) = NULL;
static void (*remove_peer_callback)(const uint8_t *mac) = NULL;

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station connected: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5]);
        if (add_peer_callback) {
            add_peer_callback(event->mac);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station disconnected: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5]);
        if (remove_peer_callback) {
            remove_peer_callback(event->mac);
        }
    }
}

esp_err_t wifi_manager_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .channel = WIFI_CHANNEL,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE)); // ESPNOW_CHANNEL

    ESP_LOGI(TAG, "WiFi AP started. SSID: %s, Password: %s, Channel: %d",
             WIFI_SSID, WIFI_PASS, WIFI_CHANNEL);

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