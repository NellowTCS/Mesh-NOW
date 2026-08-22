#include "message_queue.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#define TAG "MSG_QUEUE"
#define QUEUE_SIZE 50

static QueueHandle_t message_queue = NULL;

esp_err_t message_queue_init(void) {
    if (message_queue) {
        return ESP_OK; // Already initialized
    }

    message_queue = xQueueCreate(QUEUE_SIZE, sizeof(message_t));
    if (!message_queue) {
        ESP_LOGE(TAG, "Failed to create message queue");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Message queue initialized");
    return ESP_OK;
}

esp_err_t message_queue_deinit(void) {
    if (message_queue) {
        vQueueDelete(message_queue);
        message_queue = NULL;
        ESP_LOGI(TAG, "Message queue deinitialized");
    }
    return ESP_OK;
}

QueueHandle_t message_queue_get_handle(void) {
    return message_queue;
}

esp_err_t message_queue_send(const message_t *msg) {
    if (!message_queue || !msg) {
        return ESP_FAIL;
    }

    if (xQueueSend(message_queue, msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to send message to queue");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t message_queue_receive(message_t *msg, TickType_t timeout) {
    if (!message_queue || !msg) {
        return ESP_FAIL;
    }

    if (xQueueReceive(message_queue, msg, timeout) != pdTRUE) {
        return ESP_FAIL;
    }

    return ESP_OK;
}