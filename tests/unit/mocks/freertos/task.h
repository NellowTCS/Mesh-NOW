#ifndef MOCK_FREERTOS_TASK_H
#define MOCK_FREERTOS_TASK_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode,
                                   const char *pcName,
                                   configSTACK_DEPTH_TYPE usStackDepth,
                                   void *pvParameters, UBaseType_t uxPriority,
                                   TaskHandle_t *pvCreatedTask,
                                   BaseType_t xCoreID);
void vTaskDelete(TaskHandle_t xTaskToDelete);
void vTaskDelay(TickType_t xTicksToDelay);

#ifdef __cplusplus
}
#endif

#endif