// FreeRTOS API surface for the twin, backed by the cooperative sim scheduler.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t StackType_t;

#define pdFALSE ((BaseType_t)0)
#define pdTRUE ((BaseType_t)1)
#define pdFAIL pdFALSE
#define pdPASS pdTRUE
#define errQUEUE_FULL ((BaseType_t)0)
#define errQUEUE_EMPTY ((BaseType_t)0)

#define configTICK_RATE_HZ 1000
#define portTICK_PERIOD_MS ((TickType_t)(1000 / configTICK_RATE_HZ))
#define portTICK_RATE_MS portTICK_PERIOD_MS
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define pdMS_TO_TICKS(xTimeInMs) ((TickType_t)(((TickType_t)(xTimeInMs) * (TickType_t)configTICK_RATE_HZ) / (TickType_t)1000U))
#define pdTICKS_TO_MS(xTicks) ((TickType_t)(((uint64_t)(xTicks) * 1000U) / configTICK_RATE_HZ))
#define tskNO_AFFINITY ((BaseType_t)0x7FFFFFFF)
#define configMAX_PRIORITIES 25

typedef struct {
    uint32_t owner;
    uint32_t count;
} portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0, 0}
// Single-threaded cooperative world: critical sections cannot be interrupted.
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define portENTER_CRITICAL_ISR(mux) ((void)(mux))
#define portEXIT_CRITICAL_ISR(mux) ((void)(mux))
#define portENTER_CRITICAL_SAFE(mux) ((void)(mux))
#define portEXIT_CRITICAL_SAFE(mux) ((void)(mux))
#define taskENTER_CRITICAL(mux) ((void)(mux))
#define taskEXIT_CRITICAL(mux) ((void)(mux))
#define portYIELD_FROM_ISR(...) ((void)0)
#define spinlock_initialize(mux) ((void)(mux))

typedef struct sim_task_handle* TaskHandle_t;
typedef struct sim_queue* QueueHandle_t;
typedef QueueHandle_t SemaphoreHandle_t;

typedef struct {
    uint8_t storage[64];
} StaticSemaphore_t;
typedef StaticSemaphore_t StaticQueue_t;

BaseType_t xPortGetCoreID(void);

#ifdef __cplusplus
}
#endif
