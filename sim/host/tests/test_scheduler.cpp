// Unit tests for the cooperative scheduler and the FreeRTOS shim semantics the firmware relies on.
#include "core/scheduler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <cassert>
#include <cstdio>
#include <vector>

namespace {
std::vector<int> g_order;
QueueHandle_t g_queue;

void periodic(void* arg) {
    const int id = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    TickType_t last = xTaskGetTickCount();
    for (int i = 0; i < 5; ++i) {
        g_order.push_back(id * 100 + static_cast<int>(xTaskGetTickCount()));
        vTaskDelayUntil(&last, pdMS_TO_TICKS(20));
    }
    vTaskDelete(nullptr);
}

void producer(void*) {
    for (int i = 0; i < 3; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
        xQueueSend(g_queue, &i, 0);
    }
    vTaskDelete(nullptr);
}

int g_received = 0;
uint32_t g_receive_ms[3];
void consumer(void*) {
    int v;
    while (xQueueReceive(g_queue, &v, portMAX_DELAY) == pdPASS) {
        g_receive_ms[g_received++] = xTaskGetTickCount();
        if (g_received == 3) break;
    }
    vTaskDelete(nullptr);
}

void noop_frame(uint64_t) {}
}  // namespace

int main() {
    sim::reset(0);
    sim::set_frame_hook(noop_frame);
    // Two periodic tasks: higher priority runs first within each tick.
    xTaskCreatePinnedToCore(periodic, "low", 4096, reinterpret_cast<void*>(1), 1, nullptr, 0);
    xTaskCreatePinnedToCore(periodic, "high", 4096, reinterpret_cast<void*>(2), 4, nullptr, 0);
    sim::run_until(200000);
    const std::vector<int> expect = {200, 100, 220, 120, 240, 140, 260, 160, 280, 180};
    assert(g_order == expect);
    assert(sim::live_task_count() == 0);

    // Blocking queue receive wakes when the producer sends.
    g_queue = xQueueCreate(4, sizeof(int));
    xTaskCreatePinnedToCore(consumer, "consumer", 4096, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(producer, "producer", 4096, nullptr, 1, nullptr, 1);
    sim::run_until(300000);
    assert(g_received == 3);
    assert(g_receive_ms[0] == 210 && g_receive_ms[1] == 220 && g_receive_ms[2] == 230);

    // Busy waits advance the clock without switching tasks.
    const uint64_t before = sim::now_us();
    sim::busy_advance_us(1500);
    assert(sim::now_us() == before + 1500);
    std::puts("test_scheduler: ok");
    return 0;
}
