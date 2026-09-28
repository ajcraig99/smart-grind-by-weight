"""Run the production purge-prompt code: when CONTINUE re-tares, cup lifts, time limit."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]


class PurgeFlowTest(unittest.TestCase):
    def test_purge_prompt(self):
        source = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        header = (ROOT / "src/controllers/grind_controller.h").read_text()
        events = (ROOT / "src/controllers/grind_events.h").read_text()
        update = function(source, "void GrindController::update()")
        paused = function(update, "    if (control_loop_paused_) {")
        prime_settling = function(update, "case GrindPhase::PRIME_SETTLING: {")
        reaction = update[update.index("    // Between the purge and CONTINUE"):
                          update.index("    // Only check timeout during active grinding phases")]
        methods = "\n".join(function(source, signature) for signature in (
            "PurgeContinueResult GrindController::continue_from_purge(bool check_vessel)",
            "bool GrindController::vessel_removal_confirmed(float* sample_weight)",
        ))
        phase_enum = re.search(r"enum class GrindPhase \{.*?\};", events, re.S).group()
        result_enum = re.search(r"enum class PurgeContinueResult \{.*?\};", header, re.S).group()
        harness = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <mutex>
#include <string>
#include "src/config/grind_control.h"
#include "src/controllers/grind_session_result.h"
#include "src/controllers/net_weight_guard.h"
#define LOG_BLE(...) ((void)0)
// unsigned long is 32 bits on the ESP32; use the same width on the host.
uint32_t clock_ms = 0;
uint32_t millis() { return clock_ms; }
''' + phase_enum + "\n" + result_enum + r'''
struct GrindLoopData { uint32_t now = 0; uint32_t timestamp_ms = 0; float current_weight = 0; };
GrindLoopData at(uint32_t now, float weight = 0) { clock_ms = now; return {now, now, weight}; }
struct Sensor {
    bool fresh = true, settled = false;
    float low = 0, high = 0, settled_weight = 0, sample = 0;
    uint32_t stamp = 0;
    bool has_recent_sample() const { return fresh; }
    float get_weight_low_latency() const { return low; }
    float get_weight_high_latency() const { return high; }
    bool check_settling_complete(uint32_t, float* out) const {
        if (settled && out) *out = settled_weight;
        return settled;
    }
    bool get_latest_sample(float* weight, uint32_t* timestamp) const {
        *weight = sample; *timestamp = stamp; return true;
    }
};
struct Motor {
    bool running = false; int starts = 0;
    void start() { running = true; ++starts; }
    void stop() { running = false; }
};
struct GrindController {
    mutable std::recursive_mutex control_mutex_;
    std::unique_lock<std::recursive_mutex> lock_control() const {
        return std::unique_lock<std::recursive_mutex>(control_mutex_);
    }
    Sensor* weight_sensor = nullptr;
    Motor* grinder = nullptr;
    GrindPhase phase = GrindPhase::PURGE_CONFIRM, timeout_phase{};
    GrindSessionResult last_session_result_ = GrindSessionResult::UNKNOWN;
    NetWeightRemovalGuard net_weight_removal_guard_;
    uint32_t last_guard_sample_ms_ = 0;
    unsigned long timeout_pause_start = 0, timeout_offset_ms = 0, start_time = 0;
    unsigned long time_grind_start_ms = 0, phase_start_time = 0, last_logged_time = 0;
    float post_purge_weight_ = 0, pre_final_settled_weight_ = 0, last_logged_weight = 0;
    float grind_latency_ms = 1, finished_weight = -1;
    bool vessel_lifted_since_purge_ = false, resume_after_purge_ = false, purge_prompt_due_ = false;
    bool flow_start_confirmed = true, control_loop_paused_ = true;
    std::string error, aborted;
    GrindSessionResult abort_result = GrindSessionResult::UNKNOWN;
    GrindLoopData last_switch{};
    const char* get_phase_name(GrindPhase = GrindPhase::IDLE) const { return "PHASE"; }
    template <class... Args> void queue_log_message(Args...) {}
    void set_error_message(const char* message) { error = message; }
    void emit_progress_update(const GrindLoopData&) {}
    void switch_phase(GrindPhase next, const GrindLoopData& data = {}) {
        phase = next; last_switch = data; phase_start_time = data.now;
    }
    void abort_session(GrindSessionResult result, const char* message, const GrindLoopData&) {
        abort_result = result; aborted = message; phase = GrindPhase::TIMEOUT;
    }
    void finish_weight_grind(float weight, const GrindLoopData&) {
        finished_weight = weight; phase = GrindPhase::COMPLETED;
    }
    PurgeContinueResult continue_from_purge(bool check_vessel = true);
    bool vessel_removal_confirmed(float* sample_weight);
    void paused_tick(const GrindLoopData& loop_data) {
''' + paused + r'''
    }
    void prime_settling_tick(const GrindLoopData& loop_data) {
        switch (phase) {
''' + prime_settling + r'''
            default: break;
        }
    }
    void guard_reaction(bool vessel_removed, const GrindLoopData& loop_data) {
        float guard_sample_weight = loop_data.current_weight;
''' + reaction + r'''
        (void)guard_sample_weight;
    }
};
''' + methods + r'''
struct Rig {
    Sensor sensor; Motor motor; GrindController c;
    Rig() { c.weight_sensor = &sensor; c.grinder = &motor; }
};
// At the prompt: 1.0 g purged onto a 450 g cup; the zero includes the cup.
void at_prompt(Rig& r) {
    r.c.phase = GrindPhase::PURGE_CONFIRM;
    r.c.net_weight_removal_guard_.reset(450.0f);
    r.c.post_purge_weight_ = 1.0f;
    r.c.timeout_pause_start = 1000;
    r.sensor.low = r.sensor.high = 1.0f;
}
int main() {
    // Settled after priming: the prompt records the settled purge weight.
    { Rig r; r.c.phase = GrindPhase::PRIME_SETTLING; r.c.purge_prompt_due_ = true;
      r.sensor.settled = true; r.sensor.settled_weight = 1.05f;
      r.c.prime_settling_tick(at(2000));
      assert(r.c.phase == GrindPhase::PURGE_CONFIRM && r.c.post_purge_weight_ == 1.05f);
      assert(!r.motor.running && r.c.timeout_pause_start == 2000); }
    // Prime mode, or fresh grounds, grinds on at once.
    { Rig r; r.c.phase = GrindPhase::PRIME_SETTLING; r.sensor.settled = true;
      r.c.prime_settling_tick(at(2000));
      assert(r.c.phase == GrindPhase::PREDICTIVE && r.motor.running && r.c.time_grind_start_ms == 2000); }

    // Lifting the cup right after the purge is the purge step, not an error.
    { Rig r; r.c.phase = GrindPhase::PRIME_SETTLING; r.c.purge_prompt_due_ = true;
      r.c.guard_reaction(true, at(3000, -450.0f));
      assert(r.c.phase == GrindPhase::PURGE_CONFIRM && r.c.vessel_lifted_since_purge_);
      assert(r.c.error.empty() && r.c.timeout_pause_start == 3000); }
    // With no prompt to follow, the motor is about to restart: stop the grind.
    { Rig r; r.c.phase = GrindPhase::PRIME_SETTLING; r.motor.running = true;
      r.c.guard_reaction(true, at(3000, -450.0f));
      assert(r.c.phase == GrindPhase::TIMEOUT && r.c.error == "Err: neg wt" && !r.motor.running); }
    // At the prompt a lift is only recorded.
    { Rig r; at_prompt(r); r.c.guard_reaction(true, at(3000, -450.0f));
      assert(r.c.phase == GrindPhase::PURGE_CONFIRM && r.c.vessel_lifted_since_purge_ && r.c.error.empty()); }
    // Lifting during final settling still completes with the settled weight.
    { Rig r; r.c.phase = GrindPhase::FINAL_SETTLING; r.c.pre_final_settled_weight_ = 18.02f;
      r.c.guard_reaction(true, at(3000, -450.0f));
      assert(r.c.phase == GrindPhase::COMPLETED && r.c.finished_weight == 18.02f); }

    // The prompt counts lifts once per ADC sample and gives up after the pause limit.
    { Rig r; at_prompt(r); r.c.phase_start_time = 1000; r.sensor.sample = -440.0f;
      for (uint32_t stamp : {100u, 100u, 200u}) { r.sensor.stamp = stamp; r.c.paused_tick(at(1500)); }
      assert(!r.c.vessel_lifted_since_purge_);
      r.sensor.stamp = 300; r.c.paused_tick(at(1600));
      assert(r.c.vessel_lifted_since_purge_ && r.c.phase == GrindPhase::PURGE_CONFIRM);
      r.c.paused_tick(at(1000 + GRIND_PAUSE_MAX_MS - 1));
      assert(r.c.phase == GrindPhase::PURGE_CONFIRM && r.c.aborted.empty());
      r.c.paused_tick(at(1000 + GRIND_PAUSE_MAX_MS));
      assert(r.c.phase == GrindPhase::TIMEOUT && r.c.aborted == "Paused too long");
      assert(r.c.abort_result == GrindSessionResult::TIMEOUT); }

    // Untouched cup: the kept purge counts toward the dose, so no re-tare.
    for (float reading : {1.0f, 1.5f, 0.5f}) {
      Rig r; at_prompt(r); r.sensor.low = r.sensor.high = reading;
      assert(r.c.continue_from_purge() == PurgeContinueResult::CONTINUED);
      assert(r.c.phase == GrindPhase::PREDICTIVE && r.motor.running && !r.c.resume_after_purge_);
      assert(r.c.time_grind_start_ms == clock_ms && r.c.last_switch.now == clock_ms);
      assert(r.c.timeout_offset_ms == clock_ms - 1000 && r.c.timeout_pause_start == 0);
      assert(r.c.net_weight_removal_guard_.removal_threshold_g() == -405.0f); }
    // Lifted, or moved by more than the threshold: emptied or swapped, so re-tare.
    for (int change : {0, 1, 2}) {
      Rig r; at_prompt(r);
      if (change == 0) r.c.vessel_lifted_since_purge_ = true;
      if (change == 1) r.sensor.low = r.sensor.high = 0.4f;   // emptied, lift not seen
      if (change == 2) r.sensor.low = r.sensor.high = 31.0f;  // heavier cup
      assert(r.c.continue_from_purge() == PurgeContinueResult::CONTINUED);
      assert(r.c.phase == GrindPhase::TARING && r.c.resume_after_purge_ && !r.motor.running);
      assert(r.c.net_weight_removal_guard_.removal_threshold_g() ==
             NetWeightRemovalGuard::NO_REFERENCE_REMOVAL_THRESHOLD_G); }
    // A missing cup keeps the prompt; confirming a lighter cup re-tares.
    { Rig r; at_prompt(r); r.sensor.low = r.sensor.high = -440.0f;
      assert(r.c.continue_from_purge() == PurgeContinueResult::VESSEL_MISSING);
      assert(r.c.phase == GrindPhase::PURGE_CONFIRM && r.c.timeout_pause_start == 1000);
      assert(r.c.continue_from_purge(false) == PurgeContinueResult::CONTINUED);
      assert(r.c.phase == GrindPhase::TARING); }
    // No fresh reading: keep waiting. After the session moved on: nothing to do.
    { Rig r; at_prompt(r); r.sensor.fresh = false;
      assert(r.c.continue_from_purge() == PurgeContinueResult::SCALE_NOT_READY);
      assert(r.c.phase == GrindPhase::PURGE_CONFIRM && !r.motor.running); }
    { Rig r; at_prompt(r); r.c.phase = GrindPhase::TIMEOUT;
      assert(r.c.continue_from_purge() == PurgeContinueResult::NOT_WAITING);
      assert(r.c.phase == GrindPhase::TIMEOUT && !r.motor.running); }
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / "purge.cpp"
            binary = Path(folder) / "purge"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                            "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)

    def test_pause_limit_and_safety_stop(self):
        source = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        events = (ROOT / "src/controllers/grind_events.h").read_text()
        update = function(source, "void GrindController::update()")
        pause_limit = function(update, "case GrindPhase::TIME_GRINDING:")
        safety_stop = function(update, "    if (grinder && grinder->has_safety_stop()")
        phase_enum = re.search(r"enum class GrindPhase \{.*?\};", events, re.S).group()
        harness = r'''
#include <cassert>
#include <cstdint>
#include <string>
#include "src/config/grind_control.h"
#include "src/controllers/grind_session_result.h"
constexpr int HW_MOTOR_DEADMAN_TIMEOUT_MS = 1000;
''' + phase_enum + r'''
struct GrindLoopData { uint32_t now = 0; float current_weight = 0; };
struct Motor {
    bool running = true, latched = false;
    bool has_safety_stop() const { return latched; }
    void stop() { running = false; }
};
struct GrindController {
    Motor* grinder = nullptr;
    GrindPhase phase = GrindPhase::TIME_GRINDING, timeout_phase{};
    GrindSessionResult last_session_result_ = GrindSessionResult::UNKNOWN;
    bool grind_paused_ = false;
    uint32_t pause_start_ms_ = 0;
    float final_weight = 0;
    std::string error, aborted;
    const char* get_phase_name(GrindPhase = GrindPhase::IDLE) const { return "PHASE"; }
    template <class... Args> void queue_log_message(Args...) {}
    void set_error_message(const char* message) { error = message; }
    void switch_phase(GrindPhase next, const GrindLoopData&) { phase = next; }
    void abort_session(GrindSessionResult result, const char* message, const GrindLoopData&) {
        last_session_result_ = result; aborted = message; phase = GrindPhase::TIMEOUT;
    }
    void time_tick(const GrindLoopData& loop_data) {
        switch (phase) {
''' + pause_limit + r'''
            default: break;
        }
    }
    void safety_tick(const GrindLoopData& loop_data) {
''' + safety_stop + r'''
    }
};
int main() {
    // A paused time grind ends after the pause limit, measured from the pause.
    { GrindController c; c.grind_paused_ = true; c.pause_start_ms_ = 0xFFFFF000u;
      const uint32_t limit = GRIND_PAUSE_MAX_MS;
      c.time_tick({c.pause_start_ms_ + limit - 1});
      assert(c.phase == GrindPhase::TIME_GRINDING && c.aborted.empty());
      c.time_tick({c.pause_start_ms_ + limit});  // across the millis() wrap
      assert(c.phase == GrindPhase::TIMEOUT && c.aborted == "Paused too long");
      assert(c.last_session_result_ == GrindSessionResult::TIMEOUT); }
    { GrindController c; c.pause_start_ms_ = 0; c.time_tick({uint32_t(GRIND_PAUSE_MAX_MS * 2)});
      assert(c.phase == GrindPhase::TIME_GRINDING); }  // not paused: no limit here

    // A latched dead-man stop ends any running session, once.
    for (auto phase : {GrindPhase::PREDICTIVE, GrindPhase::TIME_GRINDING, GrindPhase::PURGE_CONFIRM}) {
      Motor motor; motor.latched = true; GrindController c; c.grinder = &motor; c.phase = phase;
      c.safety_tick({500, 12.5f});
      assert(c.phase == GrindPhase::TIMEOUT && c.timeout_phase == phase && !motor.running);
      assert(c.error == "Motor safety stop" && c.last_session_result_ == GrindSessionResult::ERROR);
      assert(c.final_weight == 12.5f);
      c.error.clear(); c.safety_tick({520, 12.5f});
      assert(c.error.empty()); }  // the result screen stays
    { Motor motor; GrindController c; c.grinder = &motor; c.phase = GrindPhase::PREDICTIVE;
      c.safety_tick({500, 1.0f}); assert(c.phase == GrindPhase::PREDICTIVE && motor.running); }
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / "pause.cpp"
            binary = Path(folder) / "pause"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                            "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
