// Deterministic cooperative scheduler and virtual clock shared by the native and WASM builds.
//
// The world advances in 1 ms frames. Within a frame, ready tasks run highest priority first
// (ties round-robin in a fixed order) until every task is blocked; then due events fire in time
// order and the frame hook (plant step) runs at the next millisecond boundary. Firmware code
// takes zero virtual time except for explicit busy waits (busy_advance_us).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace sim {

constexpr uint64_t kNever = UINT64_MAX;

struct Task;
using TaskFn = void (*)(void*);
using EventFn = void (*)(void* context);
using FrameHook = void (*)(uint64_t boundary_us);

enum class TaskState { READY, BLOCKED, SUSPENDED, DELETED };

// ---- clock ----
uint64_t now_us();
// Called by millis()/micros() shims; detects busy-wait polling and turns it into a yield.
void note_clock_read();
// Advance the clock while the current context keeps the CPU (delayMicroseconds, bit-banging).
void busy_advance_us(uint64_t us);

// ---- tasks ----
Task* task_create(const char* name, TaskFn fn, void* arg, int priority, int core, size_t stack_bytes);
// Let a newly created higher-priority task run now (FreeRTOS preemption on create).
void task_preempt_for(Task* task);
Task* current_task();  // nullptr in the main (host) context
bool in_task();
const char* task_name(const Task* task);
int task_priority(const Task* task);
int task_core(const Task* task);
TaskState task_state(const Task* task);
void task_delete(Task* task);   // nullptr deletes the calling task (does not return)
void task_suspend(Task* task);  // nullptr suspends the caller
void task_resume(Task* task);
void task_set_priority(Task* task, int priority);

// Block the calling task until `wake_us` (kNever = no timeout) or until `cond` returns true
// when the scheduler re-evaluates it. Returns true if woken by the condition.
bool block(uint64_t wake_us, const std::function<bool()>& cond = nullptr);
// Give up the CPU until the next frame.
void yield();

// ---- events (ISR-like callbacks: must not block) ----
uint64_t event_schedule(uint64_t due_us, EventFn fn, void* context);
void event_cancel(uint64_t id);

// ---- world ----
void set_frame_hook(FrameHook hook);
void run_until(uint64_t t_end_us);
// Make run_until return at the next scheduling point (restart requested).
void request_stop();
bool stop_requested();
// Drop all tasks and events and reset the clock (fresh world in the same process).
void reset(uint64_t start_us = 0);
// Number of tasks not deleted.
int live_task_count();

// Stack size used for every fiber (firmware sizes are for Xtensa; the host needs more).
constexpr size_t kFiberStackBytes = 512 * 1024;

}  // namespace sim
