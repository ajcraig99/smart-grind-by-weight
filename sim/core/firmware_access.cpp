#include "firmware_access.h"

#include "controllers/grind_controller.h"
#include "hardware/hardware_manager.h"
#include "system/state_machine.h"

// Globals and entry points defined in src/main.cpp.
extern HardwareManager hardware_manager;
extern StateMachine state_machine;
extern GrindController grind_controller;
void setup();
void loop();

namespace sim {

namespace {
bool g_booted_state_machine = false;
}

bool firmware_constructed() { return true; }

FirmwareView firmware_view() {
    FirmwareView v;
    v.phase = static_cast<int>(grind_controller.get_phase());
    v.mode = static_cast<int>(grind_controller.get_mode());
    v.target_g = grind_controller.get_target_weight();
    v.stop_offset_g = grind_controller.get_motor_stop_target_weight();
    v.latency_ms = grind_controller.get_grind_latency_ms();
    v.result = static_cast<int>(grind_controller.get_last_session_result());
    WeightSensor* sensor = hardware_manager.get_weight_sensor();
    if (sensor) {
        v.weight_g = sensor->get_weight_low_latency();
        v.flow_gps = sensor->get_flow_rate(200);
        v.scale_fault = static_cast<int>(sensor->get_hardware_fault());
        v.has_recent_sample = sensor->has_recent_sample() ? 1 : 0;
    }
    Grinder* grinder = hardware_manager.get_grinder();
    v.safety_stop = grinder && grinder->has_safety_stop() ? 1 : 0;
    v.ui_state = g_booted_state_machine ? static_cast<int>(state_machine.get_current_state()) : -1;
    return v;
}

const char* firmware_phase_name(int phase) {
    switch (static_cast<GrindPhase>(phase)) {
        case GrindPhase::IDLE: return "IDLE";
        case GrindPhase::INITIALIZING: return "INITIALIZING";
        case GrindPhase::SETUP: return "SETUP";
        case GrindPhase::TARING: return "TARING";
        case GrindPhase::TARE_CONFIRM: return "TARE_CONFIRM";
        case GrindPhase::PREDICTIVE: return "PREDICTIVE";
        case GrindPhase::PULSE_DECISION: return "PULSE_DECISION";
        case GrindPhase::PULSE_EXECUTE: return "PULSE_EXECUTE";
        case GrindPhase::PULSE_SETTLING: return "PULSE_SETTLING";
        case GrindPhase::FINAL_SETTLING: return "FINAL_SETTLING";
        case GrindPhase::TIME_GRINDING: return "TIME_GRINDING";
        case GrindPhase::MANUAL_GRINDING: return "MANUAL_GRINDING";
        case GrindPhase::TIME_ADDITIONAL_PULSE: return "TIME_ADDITIONAL_PULSE";
        case GrindPhase::COMPLETED: return "COMPLETED";
        case GrindPhase::TIMEOUT: return "TIMEOUT";
        case GrindPhase::PRIME: return "PRIME";
        case GrindPhase::PRIME_SETTLING: return "PRIME_SETTLING";
        case GrindPhase::PURGE_CONFIRM: return "PURGE_CONFIRM";
    }
    return "UNKNOWN";
}

const char* firmware_ui_state_name(int state) {
    if (state < 0) return "BOOT";
    return state_machine.get_state_name(static_cast<UIState>(state));
}

void firmware_setup() {
    setup();
    g_booted_state_machine = true;
}

void firmware_loop() { loop(); }

}  // namespace sim
