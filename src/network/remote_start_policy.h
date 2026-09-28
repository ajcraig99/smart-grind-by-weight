#pragma once

#include <cstdint>

// Admission for grind starts requested over the network (web page, Home
// Assistant). Remote starts are opt-in on the grinder, are accepted only while
// the grinder shows its main screen (not during calibration, editing, menus or
// dialogs), and are spaced so a client cannot cycle the motor rapidly.
namespace RemoteStartPolicy {

enum class Decision : uint8_t {
    ALLOW,
    SWITCHED_OFF,
    UI_BUSY,
    TOO_SOON,
};

inline Decision evaluate(bool enabled, bool ui_ready, bool has_previous_start,
                         uint32_t previous_start_ms, uint32_t now_ms,
                         uint32_t min_interval_ms) {
    if (!enabled) return Decision::SWITCHED_OFF;
    if (!ui_ready) return Decision::UI_BUSY;
    if (has_previous_start && now_ms - previous_start_ms < min_interval_ms) {
        return Decision::TOO_SOON;
    }
    return Decision::ALLOW;
}

// Short enough for the 192-byte WebSocket acknowledgement.
inline const char* rejection_reason(Decision decision) {
    switch (decision) {
        case Decision::SWITCHED_OFF:
            return "remote start is off; turn it on at the grinder (Menu > Wi-Fi)";
        case Decision::UI_BUSY:
            return "grinder is not on its main screen";
        case Decision::TOO_SOON:
            return "wait a few seconds before starting again";
        case Decision::ALLOW:
            break;
    }
    return "grind could not start";
}

}  // namespace RemoteStartPolicy
