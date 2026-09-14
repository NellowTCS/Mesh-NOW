#include "wifi_manager.h"
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#define TAG "WIFI_MGR"

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started on channel %d (ESP-NOW mesh)",
                 WIFI_CHANNEL);
    }
}

esp_err_t wifi_manager_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // STA-only, no AP; power save off so the radio listens continuously.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_ERROR_CHECK(esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));

    // Promiscuous so we can relay frames meant for other nodes (DIRECT/ACK).
    // Without it only our MAC or broadcast frames arrive (single-hop unicast).
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    ESP_LOGI(TAG, "WiFi started: STA on channel %d", WIFI_CHANNEL);
    return ESP_OK;
}

esp_err_t wifi_manager_deinit(void)
{
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_deinit();
    return ESP_OK;
}