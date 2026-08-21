#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Function pointer type for message sending callback
typedef esp_err_t (*message_send_callback_t)(const char *message);

// Function declarations
esp_err_t web_server_init(QueueHandle_t message_queue);
esp_err_t web_server_deinit(void);
void web_server_set_send_callback(message_send_callback_t callback);

#endif // WEB_SERVER_H