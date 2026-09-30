// Runtime: owns the world lifecycle, observes the firmware each millisecond, records grind
// sessions, drives the trace and the operator. The C API in sim_api.h is a thin layer on this.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../sim_api.h"
#include "firmware_access.h"

namespace sim {

struct GrindRecord {
    uint32_t index = 0;
    double t_start_s = 0;          // controller left IDLE
    double t_motor_first_s = -1;   // first relay contact in the session
    double t_end_s = 0;            // terminal phase entered (COMPLETED or TIMEOUT)
    double t_closed_s = 0;         // record closed (back to IDLE or end of run)
    int terminal_phase = -1;
    int result = 0;                // GrindSessionResult at terminal
    std::string error;             // error text the UI showed (from the firmware log)
    float fw_final_g = 0;          // weight the UI showed as the result
    bool fw_final_seen = false;
    float target_g = 0;
    double true_cup_end_g = 0;     // grounds in the cup at terminal entry
    double true_cup_closed_g = 0;  // grounds in the cup when the record closed
    double true_platform_closed_g = 0;
    double burr_left_g = 0;
    double chute_g = 0;
    double spilled_g = 0;
    double loaded_g = 0;
    int pulses = 0;                // PULSE_EXECUTE entries
    int purge_prompts = 0;         // PURGE_CONFIRM entries
    int tares = 0;                 // TARING entries
    double motor_on_s = 0;         // relay contact closed during the session
    double motor_on_max_run_s = 0; // longest continuous contact closure
    double motor_invalid_signal_s = 0; // contact closed while the load cell was faulted/unplugged
    double motor_no_sample_s = 0;  // contact closed while the firmware had no fresh sample
    double motor_after_end_s = 0;  // contact closed after the terminal phase
    double motor_no_cup_s = 0;     // contact closed while no cup was on the platform
    float latency_ms = 0;
    float stop_offset_g = 0;
    bool open = true;
};

struct RuntimeOptions {
    uint32_t trace_period_ms = 10;
    bool echo_log = false;
};

void runtime_create(uint64_t seed);
void runtime_boot();
void runtime_run_ms(uint32_t ms);
bool runtime_booted();
uint64_t runtime_now_us();
const std::vector<GrindRecord>& runtime_records();
GrindRecord* runtime_open_record();
FirmwareView runtime_last_view();
void runtime_close_records();

// Faults the runtime remembers for SAFETY accounting (the plant owns the physics).
void runtime_note_fault(int fault, int active);
bool runtime_fault_active(int fault);

// Trace (CSV rows sampled every period).
void trace_set_period_ms(uint32_t period);
void trace_on_frame(uint64_t boundary_us, const FirmwareView& view);
size_t trace_read(char* buf, size_t cap);
const std::string& trace_header();

// Operator (scripted user) - see operator.cpp.
bool operator_load_scenario_json(const std::string& text, std::string* error);
void operator_start();
bool operator_finished();
std::string operator_status();
void operator_set_target(float target_g);
void operator_seed_preferences();
void operator_on_frame();

}  // namespace sim
