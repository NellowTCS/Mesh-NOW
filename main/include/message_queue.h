#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_err.h>

// Message structure
typedef struct {
    char message[256];
    uint8_t sender_mac[6];
    uint32_t timestamp;
} message_t;

// Function declarations
esp_err_t message_queue_init(void);
esp_err_t message_queue_deinit(void);
QueueHandle_t message_queue_get_handle(void);
esp_err_t message_queue_send(const message_t *msg);
esp_err_t message_queue_receive(message_t *msg, TickType_t timeout);

#endif // MESSAGE_QUEUE_H