#include "scheduler.h"

#include "fiber.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace sim {

struct Task {
    int id = 0;
    std::string name;
    TaskFn fn = nullptr;
    void* arg = nullptr;
    int priority = 0;
    int core = 0;
    TaskState state = TaskState::READY;
    TaskState state_before_suspend = TaskState::READY;
    uint64_t wake_us = 0;
    std::function<bool()> cond;
    bool woken_by_cond = false;
    uint64_t last_run_seq = 0;
    Fiber* fiber = nullptr;
};

namespace {

struct Event {
    EventFn fn;
    void* context;
};

uint64_t g_now_us = 0;
std::vector<Task*> g_tasks;       // creation order; deleted tasks are reaped at frame edges
std::vector<Task*> g_graveyard;   // deleted tasks whose fibers cannot be freed while running
Task* g_current = nullptr;
int g_next_task_id = 1;
uint64_t g_run_seq = 0;
std::map<std::pair<uint64_t, uint64_t>, Event> g_events;  // (due_us, id) -> event
uint64_t g_next_event_id = 1;
FrameHook g_frame_hook = nullptr;
uint32_t g_clock_reads_in_slice = 0;
bool g_in_frame_processing = false;
bool g_stop_requested = false;

constexpr uint32_t kSpinReadThreshold = 20000;

uint64_t next_boundary(uint64_t t) { return (t / 1000 + 1) * 1000; }

void task_trampoline(void* arg) {
    Task* task = static_cast<Task*>(arg);
    task->fn(task->arg);
    // A FreeRTOS task must not return; Arduino-ESP32 and IDF delete it. Do the same.
    task_delete(nullptr);
}

void fire_events_until(uint64_t limit_us) {
    // Events may schedule new events (periodic timers); loop until none are due.
    while (!g_events.empty()) {
        auto it = g_events.begin();
        if (it->first.first > limit_us) break;
        const uint64_t due = it->first.first;
        Event event = it->second;
        g_events.erase(it);
        if (due > g_now_us) g_now_us = due;
        event.fn(event.context);
    }
}

// Move time forward to `target`, stepping the world at every millisecond boundary crossed.
void advance_world_to(uint64_t target) {
    if (g_in_frame_processing) {
        // A frame hook or event busy-waited; just move the clock.
        if (target > g_now_us) g_now_us = target;
        return;
    }
    g_in_frame_processing = true;
    while (g_now_us < target) {
        const uint64_t boundary = next_boundary(g_now_us);
        const uint64_t step_end = boundary < target ? boundary : target;
        fire_events_until(step_end);
        g_now_us = step_end;
        if (step_end == boundary && g_frame_hook) g_frame_hook(boundary);
    }
    g_in_frame_processing = false;
}

bool task_is_ready(Task* task) {
    if (task->state == TaskState::READY) return true;
    if (task->state != TaskState::BLOCKED) return false;
    if (task->cond && task->cond()) {
        task->woken_by_cond = true;
        return true;
    }
    return g_now_us >= task->wake_us;
}

Task* pick_ready() {
    Task* best = nullptr;
    for (Task* task : g_tasks) {
        if (!task_is_ready(task)) continue;
        if (!best || task->priority > best->priority ||
            (task->priority == best->priority && task->last_run_seq < best->last_run_seq)) {
            best = task;
        }
    }
    return best;
}

void switch_to(Task* task) {
    task->state = TaskState::READY;
    task->cond = nullptr;
    task->last_run_seq = ++g_run_seq;
    g_current = task;
    g_clock_reads_in_slice = 0;
    fiber_enter(task->fiber);
    g_current = nullptr;
}

void reap() {
    for (Task* task : g_graveyard) {
        fiber_destroy(task->fiber);
        delete task;
    }
    g_graveyard.clear();
}

void leave_current() {
    Task* self = g_current;
    fiber_leave(self->fiber);
}

}  // namespace

uint64_t now_us() { return g_now_us; }

void note_clock_read() {
    if (!g_current) return;
    if (++g_clock_reads_in_slice > kSpinReadThreshold) {
        // The task is polling the clock in a loop: let time pass as on real hardware.
        g_clock_reads_in_slice = 0;
        yield();
    }
}

void busy_advance_us(uint64_t us) {
    if (us == 0) return;
    advance_world_to(g_now_us + us);
}

Task* task_create(const char* name, TaskFn fn, void* arg, int priority, int core, size_t stack_bytes) {
    fiber_init_main();
    Task* task = new Task();
    task->id = g_next_task_id++;
    task->name = name ? name : "task";
    task->fn = fn;
    task->arg = arg;
    task->priority = priority;
    task->core = core;
    task->state = TaskState::READY;
    task->wake_us = g_now_us;
    task->fiber = fiber_create(task_trampoline, task, stack_bytes ? stack_bytes : kFiberStackBytes);
    g_tasks.push_back(task);
    return task;
}

void task_preempt_for(Task* task) {
    // FreeRTOS preempts the creator when the new task has a higher priority.
    if (g_current && task && task->priority > g_current->priority) {
        g_current->state = TaskState::BLOCKED;
        g_current->wake_us = g_now_us;  // ready again immediately, after the new task blocks
        g_current->cond = nullptr;
        leave_current();
    }
}

Task* current_task() { return g_current; }
bool in_task() { return g_current != nullptr; }
const char* task_name(const Task* task) { return task ? task->name.c_str() : "main"; }
int task_priority(const Task* task) { return task ? task->priority : 0; }
int task_core(const Task* task) { return task ? task->core : 1; }
TaskState task_state(const Task* task) { return task ? task->state : TaskState::DELETED; }

void task_delete(Task* task) {
    if (!task) task = g_current;
    if (!task) return;
    if (task->state == TaskState::DELETED) return;
    task->state = TaskState::DELETED;
    task->cond = nullptr;
    for (size_t i = 0; i < g_tasks.size(); ++i) {
        if (g_tasks[i] == task) {
            g_tasks.erase(g_tasks.begin() + static_cast<long>(i));
            break;
        }
    }
    g_graveyard.push_back(task);
    if (task == g_current) {
        leave_current();
        std::fprintf(stderr, "sim: deleted task resumed\n");
        std::abort();
    }
}

void task_suspend(Task* task) {
    if (!task) task = g_current;
    if (!task || task->state == TaskState::DELETED || task->state == TaskState::SUSPENDED) return;
    task->state_before_suspend = task->state;
    task->state = TaskState::SUSPENDED;
    if (task == g_current) leave_current();
}

void task_resume(Task* task) {
    if (!task || task->state != TaskState::SUSPENDED) return;
    // A suspended blocked task stays blocked on its original timeout (FreeRTOS removes
    // it from the delayed list; resuming makes it ready). Match FreeRTOS: ready now.
    task->state = TaskState::READY;
    task->cond = nullptr;
    task->wake_us = g_now_us;
}

void task_set_priority(Task* task, int priority) {
    if (!task) task = g_current;
    if (task) task->priority = priority;
}

bool block(uint64_t wake_us, const std::function<bool()>& cond) {
    Task* self = g_current;
    if (!self) {
        // Main (host) context: nothing to block; callers from the host only poll.
        return cond ? cond() : false;
    }
    if (cond && cond()) return true;
    self->state = TaskState::BLOCKED;
    self->wake_us = wake_us;
    self->cond = cond;
    self->woken_by_cond = false;
    leave_current();
    const bool by_cond = self->woken_by_cond;
    self->woken_by_cond = false;
    return by_cond;
}

void yield() {
    if (!g_current) return;
    block(g_now_us + 1);
}

uint64_t event_schedule(uint64_t due_us, EventFn fn, void* context) {
    const uint64_t id = g_next_event_id++;
    if (due_us < g_now_us) due_us = g_now_us;
    g_events.emplace(std::make_pair(due_us, id), Event{fn, context});
    return id;
}

void event_cancel(uint64_t id) {
    for (auto it = g_events.begin(); it != g_events.end(); ++it) {
        if (it->first.second == id) {
            g_events.erase(it);
            return;
        }
    }
}

void set_frame_hook(FrameHook hook) { g_frame_hook = hook; }

void run_until(uint64_t t_end_us) {
    if (g_current) {
        std::fprintf(stderr, "sim: run_until called from a task\n");
        std::abort();
    }
    for (;;) {
        // Run every ready task at the current instant.
        for (;;) {
            if (g_stop_requested) return;
            Task* task = pick_ready();
            if (!task) break;
            switch_to(task);
            reap();
        }
        if (g_stop_requested || g_now_us >= t_end_us) break;
        // Nothing can run now: advance to the next millisecond boundary (or the end time).
        const uint64_t boundary = next_boundary(g_now_us);
        advance_world_to(boundary < t_end_us ? boundary : t_end_us);
    }
}

void request_stop() { g_stop_requested = true; }
bool stop_requested() { return g_stop_requested; }

void reset(uint64_t start_us) {
    g_stop_requested = false;
    for (Task* task : g_tasks) {
        fiber_destroy(task->fiber);
        delete task;
    }
    g_tasks.clear();
    reap();
    g_current = nullptr;
    g_events.clear();
    g_now_us = start_us;
    g_run_seq = 0;
    g_clock_reads_in_slice = 0;
}

int live_task_count() { return static_cast<int>(g_tasks.size()); }

}  // namespace sim
