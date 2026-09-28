"""Run the production task-health check that gates confirming a new firmware image."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]


class TaskHealthTest(unittest.TestCase):
    def test_heartbeats_gate_confirmation(self):
        source = (ROOT / "src/tasks/task_manager.cpp").read_text()
        healthy = function(source, "bool TaskManager::are_tasks_healthy() const")
        harness = r'''
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include "tasks/task_heartbeat.h"
#define SYS_TASK_HEALTH_MAX_SILENCE_MS 2000
uint32_t now_ms = 0;
uint32_t millis() { return now_ms; }
using TaskHandle_t = void*;
struct TaskHandles {
    TaskHandle_t weight_sampling_task, grind_control_task, ui_render_task, bluetooth_task, file_io_task;
};
struct WeightSensor { bool fault = false; bool has_hardware_fault() const { return fault; } };
struct HardwareManager { WeightSensor sensor; WeightSensor* get_weight_sensor() { return &sensor; } };
struct TaskManager {
    bool tasks_initialized = true, ota_suspended = false;
    TaskHandles task_handles{};
    HardwareManager* hardware_manager = nullptr;
    bool are_tasks_healthy() const;
};
''' + healthy + r'''
void beat_all(uint32_t at) {
    for (auto task : {HeartbeatTask::WEIGHT_SAMPLING, HeartbeatTask::GRIND_CONTROL,
                      HeartbeatTask::UI_RENDER, HeartbeatTask::BLUETOOTH, HeartbeatTask::FILE_IO}) {
        task_heartbeats().beat(task, at);
    }
}
int main() {
    int handles[5]{};
    HardwareManager hardware;
    TaskManager tasks; tasks.hardware_manager = &hardware;
    tasks.task_handles = {&handles[0], &handles[1], &handles[2], &handles[3], &handles[4]};
    now_ms = 21000;
    assert(!tasks.are_tasks_healthy());  // Created tasks that never looped are not proof.
    beat_all(20000);
    assert(tasks.are_tasks_healthy());
    now_ms = 22000; assert(tasks.are_tasks_healthy());
    now_ms = 22001; assert(!tasks.are_tasks_healthy());  // Every task must be looping.
    beat_all(22001);
    task_heartbeats().beat(HeartbeatTask::UI_RENDER, 22005);  // Beat after the clock was read.
    assert(tasks.are_tasks_healthy());
    now_ms = 24002; beat_all(24002);
    task_heartbeats().beat(HeartbeatTask::UI_RENDER, 22001);  // UI stuck since then
    assert(!tasks.are_tasks_healthy());
    task_heartbeats().beat(HeartbeatTask::UI_RENDER, 24002);
    tasks.ota_suspended = true; assert(!tasks.are_tasks_healthy());
    tasks.ota_suspended = false; tasks.tasks_initialized = false; assert(!tasks.are_tasks_healthy());
    tasks.tasks_initialized = true; assert(tasks.are_tasks_healthy());
    // The sampling task ends by design without an HX711; any other end is a fault.
    tasks.task_handles.weight_sampling_task = nullptr;
    assert(!tasks.are_tasks_healthy());
    hardware.sensor.fault = true; assert(tasks.are_tasks_healthy());
    tasks.task_handles.grind_control_task = nullptr; assert(!tasks.are_tasks_healthy());
    // Wrap-safe across the millis() rollover.
    tasks.task_handles.grind_control_task = &handles[1];
    beat_all(0xFFFFFF00u); now_ms = 0x00000100u; assert(tasks.are_tasks_healthy());
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / "health.cpp"
            binary = Path(folder) / "health"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                            "-I", str(ROOT / "src"), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
