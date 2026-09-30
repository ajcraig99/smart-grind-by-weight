// FreeRTOS on the cooperative sim scheduler.
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "../../core/scheduler.h"

#include <cstring>
#include <deque>
#include <new>
#include <vector>

struct sim_queue {
    enum Kind { QUEUE, MUTEX, RECURSIVE_MUTEX, BINARY, COUNTING } kind = QUEUE;
    UBaseType_t length = 0;
    UBaseType_t item_size = 0;
    std::deque<std::vector<uint8_t>> items;
    // Semaphores
    UBaseType_t count = 0;
    UBaseType_t max_count = 0;
    sim::Task* owner = nullptr;
    UBaseType_t recursion = 0;
    bool static_storage = false;
};

namespace {

sim::Task* as_task(TaskHandle_t handle) { return reinterpret_cast<sim::Task*>(handle); }
TaskHandle_t as_handle(sim::Task* task) { return reinterpret_cast<TaskHandle_t>(task); }

uint64_t deadline(TickType_t ticks) {
    if (ticks == portMAX_DELAY) return sim::kNever;
    return sim::now_us() + static_cast<uint64_t>(ticks) * 1000ULL;
}

// Wait until `ready()` or the timeout. Returns ready() at the end.
template <typename F>
bool wait_for(TickType_t ticks, F ready) {
    if (ready()) return true;
    if (ticks == 0 || !sim::in_task()) return false;
    const uint64_t until = deadline(ticks);
    for (;;) {
        sim::block(until, [&ready]() { return ready(); });
        if (ready()) return true;
        if (until != sim::kNever && sim::now_us() >= until) return false;
    }
}

}  // namespace

extern "C" {

BaseType_t xPortGetCoreID(void) { return sim::task_core(sim::current_task()); }

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t, void* param,
                                   UBaseType_t priority, TaskHandle_t* created, BaseType_t core_id) {
    const int core = core_id == tskNO_AFFINITY ? 0 : static_cast<int>(core_id);
    sim::Task* task = sim::task_create(name, fn, param, static_cast<int>(priority), core, 0);
    // As in FreeRTOS, the handle is stored before the new task can preempt the creator.
    if (created) *created = as_handle(task);
    sim::task_preempt_for(task);
    return task ? pdPASS : pdFAIL;
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t depth, void* param,
                       UBaseType_t priority, TaskHandle_t* created) {
    return xTaskCreatePinnedToCore(fn, name, depth, param, priority, created, tskNO_AFFINITY);
}

void vTaskDelete(TaskHandle_t task) { sim::task_delete(as_task(task)); }

void vTaskDelay(TickType_t ticks) {
    if (!sim::in_task()) {
        sim::busy_advance_us(static_cast<uint64_t>(ticks) * 1000ULL);
        return;
    }
    if (ticks == 0) {
        sim::yield();
        return;
    }
    sim::block(sim::now_us() + static_cast<uint64_t>(ticks) * 1000ULL);
}

BaseType_t xTaskDelayUntil(TickType_t* previous_wake, TickType_t increment) {
    const TickType_t now = xTaskGetTickCount();
    const TickType_t wake = *previous_wake + increment;
    *previous_wake = wake;
    // Same overflow-aware test as FreeRTOS: delay only if the wake time is in the future.
    const TickType_t elapsed = now - (wake - increment);
    if (elapsed < increment) {
        const uint64_t base_ms = sim::now_us() / 1000ULL;
        const uint64_t wake_ms = base_ms + static_cast<uint64_t>(static_cast<TickType_t>(wake - now));
        sim::block(wake_ms * 1000ULL);
        return pdTRUE;
    }
    sim::yield();
    return pdFALSE;
}

TickType_t xTaskGetTickCount(void) { return static_cast<TickType_t>(sim::now_us() / 1000ULL); }
TickType_t xTaskGetTickCountFromISR(void) { return xTaskGetTickCount(); }
void vTaskSuspend(TaskHandle_t task) { sim::task_suspend(as_task(task)); }
void vTaskResume(TaskHandle_t task) { sim::task_resume(as_task(task)); }

eTaskState eTaskGetState(TaskHandle_t handle) {
    sim::Task* task = as_task(handle);
    if (!task) return eDeleted;
    if (task == sim::current_task()) return eRunning;
    switch (sim::task_state(task)) {
        case sim::TaskState::READY: return eReady;
        case sim::TaskState::BLOCKED: return eBlocked;
        case sim::TaskState::SUSPENDED: return eSuspended;
        case sim::TaskState::DELETED: return eDeleted;
    }
    return eInvalid;
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 2048; }
char* pcTaskGetName(TaskHandle_t task) {
    return const_cast<char*>(sim::task_name(task ? as_task(task) : sim::current_task()));
}
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return as_handle(sim::current_task()); }
UBaseType_t uxTaskPriorityGet(TaskHandle_t task) {
    return static_cast<UBaseType_t>(sim::task_priority(task ? as_task(task) : sim::current_task()));
}
void vTaskPrioritySet(TaskHandle_t task, UBaseType_t priority) {
    sim::task_set_priority(as_task(task), static_cast<int>(priority));
}
UBaseType_t uxTaskGetNumberOfTasks(void) { return static_cast<UBaseType_t>(sim::live_task_count()); }
void taskYIELD_impl(void) { sim::yield(); }

// ---- queues ----

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
    sim_queue* q = new sim_queue();
    q->kind = sim_queue::QUEUE;
    q->length = length;
    q->item_size = item_size;
    return q;
}

void vQueueDelete(QueueHandle_t queue) {
    if (queue && !queue->static_storage) delete queue;
}

static BaseType_t queue_send(QueueHandle_t q, const void* item, TickType_t ticks, bool front) {
    if (!q) return pdFAIL;
    if (!wait_for(ticks, [q]() { return q->items.size() < q->length; })) return errQUEUE_FULL;
    std::vector<uint8_t> copy(q->item_size);
    if (q->item_size && item) std::memcpy(copy.data(), item, q->item_size);
    if (front) q->items.push_front(std::move(copy));
    else q->items.push_back(std::move(copy));
    return pdPASS;
}

BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t ticks) { return queue_send(q, item, ticks, false); }
BaseType_t xQueueSendToBack(QueueHandle_t q, const void* item, TickType_t ticks) { return queue_send(q, item, ticks, false); }
BaseType_t xQueueSendToFront(QueueHandle_t q, const void* item, TickType_t ticks) { return queue_send(q, item, ticks, true); }
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void* item, BaseType_t* woken) {
    if (woken) *woken = pdFALSE;
    return queue_send(q, item, 0, false);
}

BaseType_t xQueueOverwrite(QueueHandle_t q, const void* item) {
    if (!q) return pdFAIL;
    q->items.clear();
    return queue_send(q, item, 0, false);
}

BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t ticks) {
    if (!q) return pdFAIL;
    if (!wait_for(ticks, [q]() { return !q->items.empty(); })) return errQUEUE_EMPTY;
    if (q->item_size && item) std::memcpy(item, q->items.front().data(), q->item_size);
    q->items.pop_front();
    return pdPASS;
}

BaseType_t xQueuePeek(QueueHandle_t q, void* item, TickType_t ticks) {
    if (!q) return pdFAIL;
    if (!wait_for(ticks, [q]() { return !q->items.empty(); })) return errQUEUE_EMPTY;
    if (q->item_size && item) std::memcpy(item, q->items.front().data(), q->item_size);
    return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return q ? static_cast<UBaseType_t>(q->items.size()) : 0; }
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q) {
    return q ? q->length - static_cast<UBaseType_t>(q->items.size()) : 0;
}
BaseType_t xQueueReset(QueueHandle_t q) {
    if (q) q->items.clear();
    return pdPASS;
}

// ---- semaphores ----

static sim_queue* make_sem(sim_queue::Kind kind, UBaseType_t max_count, UBaseType_t initial, void* storage) {
    static_assert(sizeof(StaticSemaphore_t) >= sizeof(void*), "storage too small");
    sim_queue* s = new sim_queue();
    (void)storage;  // Heap-allocated either way; the static buffer is left unused.
    s->kind = kind;
    s->max_count = max_count;
    s->count = initial;
    return s;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return make_sem(sim_queue::MUTEX, 1, 1, nullptr); }
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* buffer) {
    return make_sem(sim_queue::MUTEX, 1, 1, buffer);
}
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return make_sem(sim_queue::RECURSIVE_MUTEX, 1, 1, nullptr); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return make_sem(sim_queue::BINARY, 1, 0, nullptr); }
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max_count, UBaseType_t initial) {
    return make_sem(sim_queue::COUNTING, max_count, initial, nullptr);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks) {
    if (!s) return pdFAIL;
    if (s->kind == sim_queue::QUEUE) return xQueueReceive(s, nullptr, ticks);
    if (!wait_for(ticks, [s]() { return s->count > 0; })) return pdFAIL;
    --s->count;
    if (s->kind == sim_queue::MUTEX || s->kind == sim_queue::RECURSIVE_MUTEX) {
        s->owner = sim::current_task();
        s->recursion = 1;
    }
    return pdPASS;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
    if (!s) return pdFAIL;
    if (s->count >= s->max_count) return pdFAIL;
    ++s->count;
    s->owner = nullptr;
    s->recursion = 0;
    return pdPASS;
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t ticks) {
    if (!s) return pdFAIL;
    if (s->owner && s->owner == sim::current_task() && s->count == 0) {
        ++s->recursion;
        return pdPASS;
    }
    return xSemaphoreTake(s, ticks);
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s) {
    if (!s) return pdFAIL;
    if (s->recursion > 1) {
        --s->recursion;
        return pdPASS;
    }
    return xSemaphoreGive(s);
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t* woken) {
    if (woken) *woken = pdFALSE;
    return xSemaphoreGive(s);
}

void vSemaphoreDelete(SemaphoreHandle_t s) { delete s; }
UBaseType_t uxSemaphoreGetCount(SemaphoreHandle_t s) { return s ? s->count : 0; }

}  // extern "C"
