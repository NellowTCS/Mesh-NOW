#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "mesh_now.h"
#include "wifi_manager.h"
#include "serial_api.h"
#include "message_queue.h"

#define TAG           "MESH_NOW_MAIN"
#define NVS_NAMESPACE "mesh_now"
#define NVS_KEY_NAME  "node_name"

static char node_name[17] = {0};

// Callbacks for the serial API

static void app_send_broadcast(const char *message)
{
    mesh_now_send_message(message);
}

static void app_send_direct(const uint8_t *target_mac, const char *message)
{
    mesh_now_send_direct(target_mac, message);
}

static void app_send_group(uint8_t group_id, const char *message)
{
    mesh_now_send_group(group_id, message);
}

static void app_send_presence(const char *status)
{
    mesh_now_send_presence(status);
}

static void app_typing(const uint8_t *target_mac, bool typing)
{
    mesh_now_send_typing(target_mac, typing);
}

static void app_set_name(const char *name)
{
    strncpy(node_name, name, sizeof(node_name) - 1);
    node_name[sizeof(node_name) - 1] = '\0';

    mesh_now_set_name(node_name);
    mesh_now_announce_name();

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, NVS_KEY_NAME, node_name);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static void app_set_group(uint8_t group_id)
{
    mesh_now_set_group(group_id);
}

static void app_set_encryption(const uint8_t *key, size_t len)
{
    mesh_now_set_encryption_key(key, len);
}

// Mesh receive handler

static void mesh_now_receive_handler(const mesh_message_t *mesh_msg)
{
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.message, mesh_msg->message, sizeof(msg.message) - 1);
    memcpy(msg.sender_mac, mesh_msg->sender_mac, sizeof(msg.sender_mac));
    memcpy(msg.target_mac, mesh_msg->target_mac, sizeof(msg.target_mac));
    msg.timestamp = mesh_msg->timestamp;
    msg.type = mesh_msg->type;
    msg.group_id = mesh_msg->group_id;
    message_queue_send(&msg);
}

// Name management

static void load_name_from_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        size_t len = sizeof(node_name);
        if (nvs_get_str(handle, NVS_KEY_NAME, node_name, &len) != ESP_OK ||
            node_name[0] == '\0') {
            uint8_t mac[6];
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
            snprintf(node_name, sizeof(node_name), "Node-%02X%02X", mac[4],
                     mac[5]);
        }
        nvs_close(handle);
    } else {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(node_name, sizeof(node_name), "Node-%02X%02X", mac[4], mac[5]);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Mesh-NOW ESP32 Chat Application");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    load_name_from_nvs();
    ESP_LOGI(TAG, "Node name: %s", node_name);

    ESP_ERROR_CHECK(message_queue_init());
    mesh_now_set_receive_callback(mesh_now_receive_handler);
    mesh_now_set_name(node_name);

    ESP_ERROR_CHECK(wifi_manager_init());

    ESP_ERROR_CHECK(mesh_now_init());

    serial_api_callbacks_t serial_cbs = {
        .send_broadcast = app_send_broadcast,
        .send_direct = app_send_direct,
        .send_group = app_send_group,
        .send_presence = app_send_presence,
        .send_typing = app_typing,
        .set_name = app_set_name,
        .set_group = app_set_group,
        .set_encryption = app_set_encryption,
    };
    ESP_ERROR_CHECK(serial_api_init(message_queue_get_handle(), &serial_cbs));

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "Device MAC: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Connect the web UI via Web Serial over USB");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}