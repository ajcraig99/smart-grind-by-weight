#pragma once
#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TaskFunction_t)(void*);

typedef enum { eRunning = 0, eReady, eBlocked, eSuspended, eDeleted, eInvalid } eTaskState;

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack_depth,
                                   void* param, UBaseType_t priority, TaskHandle_t* created,
                                   BaseType_t core_id);
BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack_depth, void* param,
                       UBaseType_t priority, TaskHandle_t* created);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskDelayUntil(TickType_t* previous_wake, TickType_t increment);
#define vTaskDelayUntil(prev, inc) ((void)xTaskDelayUntil((prev), (inc)))
TickType_t xTaskGetTickCount(void);
TickType_t xTaskGetTickCountFromISR(void);
void vTaskSuspend(TaskHandle_t task);
void vTaskResume(TaskHandle_t task);
eTaskState eTaskGetState(TaskHandle_t task);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
char* pcTaskGetName(TaskHandle_t task);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
UBaseType_t uxTaskPriorityGet(TaskHandle_t task);
void vTaskPrioritySet(TaskHandle_t task, UBaseType_t priority);
UBaseType_t uxTaskGetNumberOfTasks(void);
void taskYIELD_impl(void);
#define taskYIELD() taskYIELD_impl()
#define portYIELD() taskYIELD_impl()

#ifdef __cplusplus
}
#endif
