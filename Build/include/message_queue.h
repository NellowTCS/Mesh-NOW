#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// A queueable message. This is the app-facing form, not the wire form.
typedef struct {
    char message[256];
    uint8_t sender_mac[6];
    uint32_t timestamp;
    uint8_t type;
    uint8_t group_id;
    uint8_t target_mac[6];
} message_t;

// Create the FreeRTOS message queue.
esp_err_t message_queue_init(void);
// Delete the message queue.
esp_err_t message_queue_deinit(void);
// Queue msg; fails if the queue is full or uninitialized.
esp_err_t message_queue_send(const message_t *msg);
// Wait up to timeout for a message into msg.
esp_err_t message_queue_receive(message_t *msg, TickType_t timeout);

// Raw queue handle, for direct FreeRTOS interaction.
QueueHandle_t message_queue_get_handle(void);

#ifdef __cplusplus
}
#endif

#endif // MESSAGE_QUEUE_H