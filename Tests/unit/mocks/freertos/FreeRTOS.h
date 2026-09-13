#ifndef MOCK_FREERTOS_H
#define MOCK_FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef uint32_t UBaseType_t;

#define portMAX_DELAY 0xffffffffUL
#define pdPASS        1
#define pdTRUE        1
#define pdFALSE       0

#define portTICK_PERIOD_MS 10
#define pdMS_TO_TICKS(ms)  ((TickType_t)(ms))

typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *pvParameters);
typedef unsigned int configSTACK_DEPTH_TYPE;

#endif