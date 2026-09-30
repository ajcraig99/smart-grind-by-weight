#pragma once
#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
void vQueueDelete(QueueHandle_t queue);
BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t ticks_to_wait);
BaseType_t xQueueSendToBack(QueueHandle_t queue, const void* item, TickType_t ticks_to_wait);
BaseType_t xQueueSendToFront(QueueHandle_t queue, const void* item, TickType_t ticks_to_wait);
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void* item, BaseType_t* woken);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void* item);
BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t ticks_to_wait);
BaseType_t xQueuePeek(QueueHandle_t queue, void* item, TickType_t ticks_to_wait);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t queue);
BaseType_t xQueueReset(QueueHandle_t queue);

#ifdef __cplusplus
}
#endif
