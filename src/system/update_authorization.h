#pragma once

#include <atomic>
#include <cstdint>

// On-device permission for one firmware update. The touchscreen grants it for
// a short window; the web and Bluetooth update paths consume it when an update
// starts, so a device on the network or in radio range cannot replace the
// firmware without someone at the grinder.
class UpdateAuthorization {
public:
    void grant(uint32_t now_ms, uint32_t window_ms) {
        deadline_ms_.store(now_ms + window_ms);
        granted_.store(true);
    }

    void revoke() { granted_.store(false); }

    bool is_granted(uint32_t now_ms) const {
        return granted_.load() && static_cast<int32_t>(deadline_ms_.load() - now_ms) > 0;
    }

    uint32_t remaining_ms(uint32_t now_ms) const {
        return is_granted(now_ms) ? deadline_ms_.load() - now_ms : 0;
    }

    // Takes the permission for one update. False when none was granted or the
    // window has passed; either way the permission is gone afterwards.
    bool consume(uint32_t now_ms) {
        bool expected = true;
        if (!granted_.compare_exchange_strong(expected, false)) return false;
        return static_cast<int32_t>(deadline_ms_.load() - now_ms) > 0;
    }

private:
    std::atomic<bool> granted_{false};
    std::atomic<uint32_t> deadline_ms_{0};
};

inline UpdateAuthorization& update_authorization() {
    static UpdateAuthorization authorization;
    return authorization;
}
