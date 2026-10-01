// Read-only observation of the firmware's global objects (defined in src/main.cpp), plus
// the Arduino entry points. Observation never mutates firmware state.
#pragma once

#include <cstdint>

namespace sim {

struct FirmwareView {
    int phase = -1;           // GrindPhase
    int ui_state = -1;        // UIState
    int mode = 0;             // GrindMode
    float weight_g = 0;       // WeightSensor::get_weight_low_latency
    float flow_gps = 0;       // WeightSensor::get_flow_rate(200)
    float target_g = 0;
    float stop_offset_g = 0;
    float latency_ms = 0;
    int result = 0;           // GrindSessionResult
    int scale_fault = 0;      // WeightSensor::HardwareFault
    int safety_stop = 0;
    int has_recent_sample = 0;
    int pulse_count = 0;      // not exposed by the controller; derived by the runtime from phases
};

bool firmware_constructed();
FirmwareView firmware_view();
const char* firmware_phase_name(int phase);
const char* firmware_ui_state_name(int state);
// Centre of the confirm dialog's left (confirm) or right (cancel) button on the panel, so the
// scripted user can tap it. False if the dialog is not shown.
bool firmware_confirm_button_center(bool confirm, int* x, int* y);

// Arduino sketch entry points from src/main.cpp.
void firmware_setup();
void firmware_loop();

}  // namespace sim
