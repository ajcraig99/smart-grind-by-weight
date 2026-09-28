#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// Loop heartbeats of the long-running tasks. A task that was created but has
// stopped looping (deadlocked, or stuck in a driver call) must not let a new
// firmware image be confirmed.
enum class HeartbeatTask : uint8_t {
    WEIGHT_SAMPLING,
    GRIND_CONTROL,
    UI_RENDER,
    BLUETOOTH,
    FILE_IO,
    COUNT
};

class TaskHeartbeats {
public:
    void beat(HeartbeatTask task, uint32_t now_ms) {
        const size_t index = static_cast<size_t>(task);
        last_beat_ms_[index].store(now_ms);
        seen_[index].store(true);
    }

    // True when the task finished a loop within max_age_ms of now_ms. A beat
    // stored after now_ms was read also counts.
    bool recent(HeartbeatTask task, uint32_t now_ms, uint32_t max_age_ms) const {
        const size_t index = static_cast<size_t>(task);
        return seen_[index].load() &&
               static_cast<int32_t>(now_ms - last_beat_ms_[index].load()) <=
                   static_cast<int32_t>(max_age_ms);
    }

private:
    static constexpr size_t COUNT = static_cast<size_t>(HeartbeatTask::COUNT);
    std::atomic<uint32_t> last_beat_ms_[COUNT]{};
    std::atomic<bool> seen_[COUNT]{};
};

inline TaskHeartbeats& task_heartbeats() {
    static TaskHeartbeats heartbeats;
    return heartbeats;
}
