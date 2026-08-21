#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

typedef void (*web_send_broadcast_cb)(const char *message);
typedef void (*web_send_direct_cb)(const uint8_t *target_mac, const char *message);
typedef void (*web_send_group_cb)(uint8_t group_id, const char *message);
typedef void (*web_send_presence_cb)(const char *status);
typedef void (*web_typing_cb)(const uint8_t *target_mac, bool typing);
typedef void (*web_set_name_cb)(const char *name);
typedef void (*web_set_group_cb)(uint8_t group_id);
typedef void (*web_set_encryption_cb)(const uint8_t *key, size_t len);

typedef struct {
    web_send_broadcast_cb send_broadcast;
    web_send_direct_cb send_direct;
    web_send_group_cb send_group;
    web_send_presence_cb send_presence;
    web_typing_cb send_typing;
    web_set_name_cb set_name;
    web_set_group_cb set_group;
    web_set_encryption_cb set_encryption;
} web_server_callbacks_t;

esp_err_t web_server_init(QueueHandle_t message_queue, const web_server_callbacks_t *callbacks);
esp_err_t web_server_deinit(void);

#endif // WEB_SERVER_H
