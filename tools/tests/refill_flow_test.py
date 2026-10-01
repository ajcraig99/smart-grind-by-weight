"""Run the production out-of-beans code: pause, settle-before-compare CONTINUE, limits."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]


class RefillFlowTest(unittest.TestCase):
    def test_refill_prompt(self):
        source = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        header = (ROOT / "src/controllers/grind_controller.h").read_text()
        events = (ROOT / "src/controllers/grind_events.h").read_text()
        update = function(source, "void GrindController::update()")
        paused = function(update, "    if (control_loop_paused_) {")
        # Only the dry-run check at the top of PREDICTIVE; the strategy is not under test.
        predictive_dry_run = function(update, "case GrindPhase::PREDICTIVE:")
        methods = "\n".join(function(source, signature) for signature in (
            "RefillContinueResult GrindController::continue_from_refill(bool accept_current_reading)",
            "bool GrindController::decline_refill()",
            "RefillContinueResult GrindController::take_refill_outcome()",
            "RefillPromptInfo GrindController::get_refill_prompt_info() const",
            "bool GrindController::vessel_removal_confirmed(float* sample_weight)",
            "bool GrindController::dry_run_detected(const GrindLoopData& loop_data)",
            "void GrindController::end_or_pause_dry_run(const GrindLoopData& loop_data)",
            "void GrindController::enter_refill_pause(const GrindLoopData& loop_data)",
            "void GrindController::refill_pause_tick(const GrindLoopData& loop_data, bool vessel_lifted)",
            "RefillContinueResult GrindController::evaluate_refill_continue(",
            "void GrindController::resume_after_refill(const GrindLoopData& loop_data)",
            "void GrindController::abort_session(GrindSessionResult result, const char* message,",
            "bool GrindController::check_timeout() const",
        ))
        phase_enum = re.search(r"enum class GrindPhase \{.*?\};", events, re.S).group()
        result_enum = re.search(r"enum class RefillContinueResult \{.*?\};", header, re.S).group()
        info_struct = re.search(r"struct RefillPromptInfo \{.*?\};", header, re.S).group()
        harness = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>
#include "src/config/grind_control.h"
#include "src/controllers/grind_mode.h"
#include "src/controllers/grind_session_result.h"
#include "src/controllers/net_weight_guard.h"
#define LOG_BLE(...) ((void)0)
// unsigned long is 32 bits on the ESP32; use the same width on the host.
uint32_t clock_ms = 0;
uint32_t millis() { return clock_ms; }
''' + phase_enum + "\n" + result_enum + "\n" + info_struct + r'''
struct GrindLoopData {
    uint32_t now = 0, timestamp_ms = 0; float current_weight = 0, weight_delta = 0, flow_rate = 0;
    uint8_t phase_id = 0;
};
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
    // The cup put down at `grams` on the existing zero, scale at rest.
    void rest(float grams) { low = high = settled_weight = sample = grams; settled = true; }
};
struct Motor {
    bool running = false; int starts = 0;
    void start() { running = true; ++starts; }
    void stop() { running = false; }
};
struct Logger {
    std::vector<int> motor_states;
    void log_continuous_measurement(uint32_t, float, float, float, uint8_t motor, uint8_t, float) {
        motor_states.push_back(motor);
    }
} grind_logger;
struct GrindController {
    mutable std::recursive_mutex control_mutex_;
    std::unique_lock<std::recursive_mutex> lock_control() const {
        return std::unique_lock<std::recursive_mutex>(control_mutex_);
    }
    Sensor* weight_sensor = nullptr;
    Motor* grinder = nullptr;
    GrindMode mode = GrindMode::WEIGHT;
    GrindPhase phase = GrindPhase::PREDICTIVE, timeout_phase{};
    GrindSessionResult last_session_result_ = GrindSessionResult::UNKNOWN;
    NetWeightRemovalGuard net_weight_removal_guard_;
    uint32_t last_guard_sample_ms_ = 0;
    unsigned long start_time = 0, phase_start_time = 0, time_grind_start_ms = 0;
    unsigned long timeout_pause_start = 0, timeout_offset_ms = 0, last_logged_time = 0;
    bool grind_paused_ = false; uint32_t pause_start_ms_ = 0;
    float target_weight = 18.0f, final_weight = 0, last_logged_weight = 0;
    float grind_latency_ms = 420, motor_stop_target_weight = 0.4f;
    int pulse_attempts = 0;
    bool flow_start_confirmed = true, control_loop_paused_ = false, vessel_lifted_since_purge_ = false;
    float dry_run_reference_weight_ = 0; unsigned long dry_run_reference_ms_ = 0;
    uint8_t refill_resume_count_ = 0;
    GrindPhase refill_resume_phase_ = GrindPhase::PREDICTIVE;
    bool refill_no_beans_at_start_ = false;
    float refill_pause_weight_ = 0;
    bool refill_reference_settled_ = false, refill_vessel_lifted_ = false, refill_continue_pending_ = false;
    unsigned long refill_continue_requested_ms_ = 0;
    RefillContinueResult refill_outcome_ = RefillContinueResult::NONE;
    bool refill_leg_resumed_ = false;
    float refill_leg_start_weight_ = 0;
    int session_pulse_total_ = 0;
    float peak_healthy_stop_offset_g_ = 0, peak_healthy_flow_gps_ = 0, pulse_flow_rate = 0;
    bool logging = true;
    std::string error;
    const char* get_phase_name(GrindPhase = GrindPhase::IDLE) const { return "PHASE"; }
    template <class... Args> void queue_log_message(Args...) {}
    void set_error_message(const char* message) { error = message; }
    void emit_progress_update(const GrindLoopData&) {}
    bool should_log_measurements() const { return logging; }
    // As the real switch_phase: pause flag, phase clock and dry-run window.
    void switch_phase(GrindPhase next, const GrindLoopData& data = {}) {
        phase = next; phase_start_time = data.now;
        control_loop_paused_ = next == GrindPhase::PURGE_CONFIRM || next == GrindPhase::REFILL_CONFIRM;
        if (next == GrindPhase::PRIME || next == GrindPhase::PREDICTIVE) {
            dry_run_reference_weight_ = weight_sensor ? weight_sensor->get_weight_low_latency() : 0.0f;
            dry_run_reference_ms_ = data.now;
        }
    }
    RefillContinueResult continue_from_refill(bool accept_current_reading = false);
    bool decline_refill();
    RefillContinueResult take_refill_outcome();
    RefillPromptInfo get_refill_prompt_info() const;
    bool vessel_removal_confirmed(float* sample_weight);
    bool dry_run_detected(const GrindLoopData& loop_data);
    void end_or_pause_dry_run(const GrindLoopData& loop_data);
    void enter_refill_pause(const GrindLoopData& loop_data);
    void refill_pause_tick(const GrindLoopData& loop_data, bool vessel_lifted);
    RefillContinueResult evaluate_refill_continue(float settled_weight, const GrindLoopData& loop_data);
    void resume_after_refill(const GrindLoopData& loop_data);
    void abort_session(GrindSessionResult result, const char* message, const GrindLoopData& loop_data);
    bool check_timeout() const;
    GrindLoopData at(uint32_t now) {
        clock_ms = now;
        GrindLoopData d; d.now = now; d.timestamp_ms = now - start_time;
        d.current_weight = weight_sensor->get_weight_low_latency();
        return d;
    }
    // One control tick: the real paused branch, then the real PREDICTIVE dry-run check.
    void tick(uint32_t now) {
        GrindLoopData loop_data = at(now);
''' + paused + r'''
        switch (phase) {
''' + predictive_dry_run + r'''
            default: break;
        }
    }
};
''' + methods + r'''
struct Rig {
    Sensor sensor; Motor motor; GrindController c;
    // Grinding in PREDICTIVE onto a 450 g cup (the zero includes it); the burrs
    // ran dry at 12.0 g, so the reading stopped rising at t = 1000.
    Rig() {
        c.weight_sensor = &sensor; c.grinder = &motor;
        c.net_weight_removal_guard_.reset(450.0f);
        motor.running = true; sensor.rest(12.0f); sensor.settled = false;
        c.start_time = 0; c.pulse_attempts = 4;
        c.switch_phase(GrindPhase::PREDICTIVE, c.at(1000));
    }
    // Lift the cup off for three fresh ADC samples (confirmed removal).
    void lift() {
        for (int i = 0; i < 3; ++i) { sensor.sample = -440.0f; sensor.stamp += 100; c.tick(clock_ms + 100); }
    }
};
// Runs out of beans: 5 s without 0.2 g of progress.
void run_out(Rig& r) {
    r.c.tick(1000 + GRIND_DRY_RUN_TIMEOUT_MS - 1);
    assert(r.c.phase == GrindPhase::PREDICTIVE);
    r.c.tick(1000 + GRIND_DRY_RUN_TIMEOUT_MS);
    assert(r.c.phase == GrindPhase::REFILL_CONFIRM);
}
void assert_resumed(Rig& r, int starts_before) {
    assert(r.c.phase == GrindPhase::PREDICTIVE && r.motor.running && r.motor.starts == starts_before + 1);
    assert(!r.c.control_loop_paused_ && !r.c.refill_continue_pending_);
    assert(r.c.timeout_offset_ms == clock_ms - r.c.start_time && r.c.timeout_pause_start == 0);
    assert(!r.c.flow_start_confirmed && r.c.grind_latency_ms == 0);
    assert(r.c.motor_stop_target_weight == GRIND_UNDERSHOOT_TARGET_G);
    assert(r.c.pulse_attempts == 0 && r.c.refill_leg_resumed_ && r.c.time_grind_start_ms == clock_ms);
}
int main() {
    // Running out mid-grind pauses with the motor off instead of ending the grind.
    { Rig r; run_out(r);
      const uint32_t paused_at = 1000 + GRIND_DRY_RUN_TIMEOUT_MS;
      assert(!r.motor.running && r.c.control_loop_paused_ && r.c.error.empty());
      assert(r.c.timeout_pause_start == paused_at && r.c.refill_resume_phase_ == GrindPhase::PREDICTIVE);
      assert(!r.c.refill_no_beans_at_start_ && r.c.refill_pause_weight_ == 12.0f);
      // The motor stopping is logged, so the pause is not counted as motor time.
      assert(!grind_logger.motor_states.empty() && grind_logger.motor_states.back() == 0);
      RefillPromptInfo info = r.c.get_refill_prompt_info();
      assert(!info.no_beans_at_start && info.target_weight_g == 18.0f && !info.waiting_for_settle); }

    // No beans at all: the dry run in PRIME offers the prompt and resumes in PRIME.
    { Rig r; r.sensor.rest(0.05f); r.sensor.settled = false;
      r.c.switch_phase(GrindPhase::PRIME, r.c.at(1000));
      r.c.end_or_pause_dry_run(r.c.at(6000));
      assert(r.c.phase == GrindPhase::REFILL_CONFIRM && !r.motor.running);
      assert(r.c.refill_no_beans_at_start_ && r.c.refill_resume_phase_ == GrindPhase::PRIME);
      r.sensor.settled = true;
      assert(r.c.continue_from_refill() == RefillContinueResult::CONTINUED);
      assert(r.c.phase == GrindPhase::PRIME && r.motor.running); }

    // While paused nothing starts the motor; the reference becomes the settled
    // reading once the scale is at rest, and a lift is only recorded.
    { Rig r; run_out(r);
      for (int i = 0; i < 200; ++i) {
          r.sensor.low = 12.0f + (i % 7) * 0.3f; r.c.tick(clock_ms + 20);
      }
      assert(r.c.phase == GrindPhase::REFILL_CONFIRM && r.motor.starts == 0 && !r.motor.running);
      assert(!r.c.refill_reference_settled_);
      r.sensor.rest(12.08f); r.c.tick(clock_ms + 20);
      assert(r.c.refill_reference_settled_ && r.c.refill_pause_weight_ == 12.08f);
      r.lift();
      assert(r.c.phase == GrindPhase::REFILL_CONFIRM && r.c.refill_vessel_lifted_ && r.c.error.empty());
      assert(r.motor.starts == 0); }
    // After a lift, a later settled reading does not replace the reference.
    { Rig r; run_out(r); r.lift(); r.sensor.rest(30.0f); r.c.tick(clock_ms + 20);
      assert(!r.c.refill_reference_settled_ && r.c.refill_pause_weight_ == 12.0f); }

    // CONTINUE on a settled scale within 0.5 g: grinds on from the existing zero.
    for (float reading : {12.0f, 12.5f, 11.5f}) {
      Rig r; run_out(r); r.sensor.rest(12.0f); r.c.tick(clock_ms + 600);  // reference 12.0 g
      r.sensor.rest(reading);
      clock_ms += 100000;  // a long refill does not count against the grind timeout
      assert(r.c.continue_from_refill() == RefillContinueResult::CONTINUED);
      assert_resumed(r, 0);
      assert(r.c.refill_resume_count_ == 1 && r.c.session_pulse_total_ == 4);
      assert(r.c.refill_leg_start_weight_ == reading && !r.c.check_timeout());
      const uint32_t resumed = clock_ms;
      clock_ms = resumed + GRIND_TIMEOUT_SEC * 1000 - 1; assert(!r.c.check_timeout());
      clock_ms = resumed + GRIND_TIMEOUT_SEC * 1000; assert(r.c.check_timeout()); }
    // A stretch too short to measure its own stop offset starts from the peak
    // measured on healthy flow, never below the default.
    for (float healthy : {0.0f, 0.8f, 1.3f}) {
      Rig r; run_out(r); r.c.peak_healthy_stop_offset_g_ = healthy; r.sensor.rest(12.0f);
      assert(r.c.continue_from_refill() == RefillContinueResult::CONTINUED);
      assert(r.c.phase == GrindPhase::PREDICTIVE);
      assert(r.c.motor_stop_target_weight == std::max(GRIND_UNDERSHOOT_TARGET_G, healthy)); }
    // Pulses after the refill start from the full flow measured earlier, never lower.
    for (float flow : {0.0f, 2.4f}) {
      Rig r; run_out(r); r.c.peak_healthy_flow_gps_ = flow; r.c.pulse_flow_rate = 1.2f; r.sensor.rest(12.0f);
      assert(r.c.continue_from_refill() == RefillContinueResult::CONTINUED);
      assert(r.c.pulse_flow_rate == std::max(1.2f, flow)); }
    // With the dose already within the stop offset, it goes straight to the
    // correction pulses: the motor is not started as a continuous run.
    for (float reading : {17.2f, 17.0f, 17.98f}) {
      Rig r; r.sensor.rest(reading); r.sensor.settled = false;
      r.c.switch_phase(GrindPhase::PREDICTIVE, r.c.at(1000));
      run_out(r); r.sensor.rest(reading);
      assert(r.c.continue_from_refill() == RefillContinueResult::CONTINUED);
      assert(r.c.phase == GrindPhase::PULSE_SETTLING && !r.motor.running && r.motor.starts == 0);
      assert(r.c.refill_resume_count_ == 1 && r.c.pulse_attempts == 0 && !r.c.control_loop_paused_);
      // Pulse settling waits latency + 200 ms; the latency measured at the start is kept.
      assert(r.c.grind_latency_ms == 420); }
    { Rig r; r.sensor.rest(16.9f); r.sensor.settled = false;  // 1.1 g to go: PREDICTIVE can stop in time
      r.c.switch_phase(GrindPhase::PREDICTIVE, r.c.at(1000));
      run_out(r); r.sensor.rest(16.9f);
      assert(r.c.continue_from_refill() == RefillContinueResult::CONTINUED); assert_resumed(r, 0); }
    // Lifted and put back where it was still keeps the zero.
    { Rig r; run_out(r); r.sensor.rest(12.0f); r.c.tick(clock_ms + 600); r.lift();
      r.sensor.rest(12.2f);
      assert(r.c.continue_from_refill() == RefillContinueResult::CONTINUED); assert_resumed(r, 0); }

    // CONTINUE while the scale is moving waits for it to settle, motor off.
    { Rig r; run_out(r); r.sensor.rest(12.0f); r.c.tick(clock_ms + 600);
      r.sensor.settled = false; r.sensor.low = 12.6f;  // still pouring
      const uint32_t pressed = clock_ms + 20; clock_ms = pressed;
      assert(r.c.continue_from_refill() == RefillContinueResult::WAITING_FOR_SETTLE);
      assert(r.c.get_refill_prompt_info().waiting_for_settle && !r.motor.running);
      assert(r.c.continue_from_refill() == RefillContinueResult::WAITING_FOR_SETTLE);  // repeat taps
      for (uint32_t t = pressed + 20; t < pressed + 2000; t += 20) r.c.tick(t);
      assert(r.c.phase == GrindPhase::REFILL_CONFIRM && r.motor.starts == 0);
      assert(r.c.take_refill_outcome() == RefillContinueResult::NONE);
      r.sensor.rest(12.3f); r.c.tick(pressed + 2020);
      assert_resumed(r, 0);
      assert(r.c.take_refill_outcome() == RefillContinueResult::CONTINUED);
      assert(r.c.take_refill_outcome() == RefillContinueResult::NONE); }
    // ...and gives up, without starting the motor, if it never settles.
    { Rig r; run_out(r); r.sensor.rest(12.0f); r.c.tick(clock_ms + 600); r.sensor.settled = false;
      const uint32_t pressed = clock_ms + 20; clock_ms = pressed;
      assert(r.c.continue_from_refill() == RefillContinueResult::WAITING_FOR_SETTLE);
      r.c.tick(pressed + GRIND_REFILL_SETTLE_TIMEOUT_MS - 1);
      assert(r.c.refill_continue_pending_ && r.c.take_refill_outcome() == RefillContinueResult::NONE);
      r.c.tick(pressed + GRIND_REFILL_SETTLE_TIMEOUT_MS);
      assert(!r.c.refill_continue_pending_ && r.c.take_refill_outcome() == RefillContinueResult::SETTLE_TIMEOUT);
      r.sensor.rest(12.0f); r.c.tick(clock_ms + 20);
      assert(r.c.phase == GrindPhase::REFILL_CONFIRM && r.motor.starts == 0 && !r.motor.running);
      assert(r.c.continue_from_refill() == RefillContinueResult::CONTINUED); }  // a new press works
    // A lift while waiting drops the CONTINUE: putting the cup back never starts the motor.
    { Rig r; run_out(r); r.sensor.rest(12.0f); r.c.tick(clock_ms + 600); r.sensor.settled = false;
      assert(r.c.continue_from_refill() == RefillContinueResult::WAITING_FOR_SETTLE);
      r.lift();
      assert(r.c.take_refill_outcome() == RefillContinueResult::CUP_LIFTED && !r.c.refill_continue_pending_);
      r.sensor.rest(12.0f); for (int i = 0; i < 50; ++i) r.c.tick(clock_ms + 20);
      assert(r.c.phase == GrindPhase::REFILL_CONFIRM && r.motor.starts == 0); }

    // A reading that moved more than 0.5 g, or a missing cup, needs the user's word.
    { Rig r; run_out(r); r.sensor.rest(12.0f); r.c.tick(clock_ms + 600);
      r.sensor.rest(3.1f);  // cup emptied, or a lighter one
      assert(r.c.continue_from_refill() == RefillContinueResult::READING_MOVED);
      assert(r.c.phase == GrindPhase::REFILL_CONFIRM && !r.motor.running);
      assert(r.c.continue_from_refill(true) == RefillContinueResult::CONTINUED); assert_resumed(r, 0); }
    // CONTINUE in the "Cup moved?" dialog still refuses if the cup has gone since.
    { Rig r; run_out(r); r.sensor.rest(12.0f); r.c.tick(clock_ms + 600);
      r.sensor.rest(3.1f);
      assert(r.c.continue_from_refill() == RefillContinueResult::READING_MOVED);
      r.sensor.rest(-440.0f);  // cup taken away while the dialog was open
      assert(r.c.continue_from_refill(true) == RefillContinueResult::VESSEL_MISSING);
      assert(r.c.phase == GrindPhase::REFILL_CONFIRM && !r.motor.running && r.motor.starts == 0); }
    { Rig r; run_out(r); r.sensor.rest(-440.0f);
      assert(r.c.continue_from_refill() == RefillContinueResult::VESSEL_MISSING && !r.motor.running);
      // Without the cup there is nothing to continue onto, whatever the dialog said.
      assert(r.c.continue_from_refill(true) == RefillContinueResult::VESSEL_MISSING);
      assert(r.c.phase == GrindPhase::REFILL_CONFIRM && r.motor.starts == 0); }
    // A moved reading found after waiting to settle is reported, not acted on.
    { Rig r; run_out(r); r.sensor.rest(12.0f); r.c.tick(clock_ms + 600); r.sensor.settled = false;
      assert(r.c.continue_from_refill() == RefillContinueResult::WAITING_FOR_SETTLE);
      r.sensor.rest(13.0f); r.c.tick(clock_ms + 20);
      assert(r.c.take_refill_outcome() == RefillContinueResult::READING_MOVED && r.motor.starts == 0); }
    // No fresh reading: keep waiting. Not at the prompt: nothing to do.
    { Rig r; run_out(r); r.sensor.fresh = false; r.sensor.settled = true;
      assert(r.c.continue_from_refill() == RefillContinueResult::SCALE_NOT_READY && !r.motor.running);
      assert(r.c.continue_from_refill(true) == RefillContinueResult::SCALE_NOT_READY && !r.motor.running); }
    { Rig r; r.sensor.settled = true;
      assert(r.c.continue_from_refill(true) == RefillContinueResult::NOT_WAITING);
      assert(r.motor.starts == 0 && !r.c.decline_refill() && r.c.phase == GrindPhase::PREDICTIVE); }

    // STOP, or no answer for 5 minutes, ends the grind as today's "No beans?".
    { Rig r; run_out(r); assert(r.c.decline_refill());
      assert(r.c.phase == GrindPhase::TIMEOUT && r.c.error == "No beans?" && !r.motor.running);
      assert(r.c.last_session_result_ == GrindSessionResult::ERROR); }
    { Rig r; run_out(r); const uint32_t paused_at = clock_ms;
      r.c.tick(paused_at + GRIND_PAUSE_MAX_MS - 1); assert(r.c.phase == GrindPhase::REFILL_CONFIRM);
      r.c.tick(paused_at + GRIND_PAUSE_MAX_MS);
      assert(r.c.phase == GrindPhase::TIMEOUT && r.c.error == "No beans?");
      assert(r.c.last_session_result_ == GrindSessionResult::ERROR && r.motor.starts == 0); }

    // Repeat run-outs pause again until GRIND_REFILL_MAX_RESUMES is used up.
    { Rig r; r.c.target_weight = 40.0f;  // a large dose, so every resume is a PREDICTIVE stretch
      for (int refill = 1; refill <= GRIND_REFILL_MAX_RESUMES; ++refill) {
          const uint32_t t0 = clock_ms;
          r.sensor.low = r.sensor.high = 12.0f + refill * 3.0f;  // the new beans made progress
          r.c.dry_run_reference_weight_ = r.sensor.low; r.c.dry_run_reference_ms_ = t0;
          r.c.tick(t0 + GRIND_DRY_RUN_TIMEOUT_MS);
          assert(r.c.phase == GrindPhase::REFILL_CONFIRM);
          r.sensor.rest(r.sensor.low);
          assert(r.c.continue_from_refill() == RefillContinueResult::CONTINUED);
          assert(r.c.refill_resume_count_ == refill);
          r.sensor.settled = false; r.sensor.low += 2.0f;
          r.c.tick(clock_ms + 100);
      }
      const uint32_t t0 = clock_ms;
      r.c.dry_run_reference_weight_ = r.sensor.low; r.c.dry_run_reference_ms_ = t0;
      r.c.tick(t0 + GRIND_DRY_RUN_TIMEOUT_MS);
      assert(r.c.phase == GrindPhase::TIMEOUT && r.c.error == "No beans?" && !r.motor.running); }
    // A resumed stretch that gains nothing is a jam or open relay, not beans: no second prompt.
    { Rig r; run_out(r); r.sensor.rest(12.0f);
      assert(r.c.continue_from_refill() == RefillContinueResult::CONTINUED);
      r.sensor.settled = false; r.sensor.low = 12.1f;
      r.c.tick(clock_ms + GRIND_DRY_RUN_TIMEOUT_MS);
      assert(r.c.phase == GrindPhase::TIMEOUT && r.c.error == "No beans?" && !r.motor.running);
      assert(r.motor.starts == 1); }
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / "refill.cpp"
            binary = Path(folder) / "refill"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                            "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)

    def test_refill_wiring(self):
        controller = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        update = function(controller, "void GrindController::update()")
        # Both dry-run sites go through the refill decision, never straight to an abort.
        for case in ("case GrindPhase::PRIME: {", "case GrindPhase::PREDICTIVE:"):
            block = function(update, case)
            self.assertIn("end_or_pause_dry_run(loop_data);", block, case)
        switch = function(controller, "void GrindController::switch_phase(")
        self.assertIn("phase == GrindPhase::REFILL_CONFIRM", switch)  # pauses the control loop
        logging = function(controller, "bool GrindController::should_log_measurements() const")
        self.assertIn("GrindPhase::REFILL_CONFIRM", logging)
        # The motor starts in exactly one refill function, and nowhere on the network side.
        starts = [name for name in ("continue_from_refill", "refill_pause_tick", "evaluate_refill_continue",
                                    "enter_refill_pause", "end_or_pause_dry_run", "decline_refill")
                  if "grinder->start()" in function(controller, "GrindController::" + name + "(")]
        self.assertEqual(starts, [])
        self.assertIn("grinder->start()", function(controller, "void GrindController::resume_after_refill("))
        api = (ROOT / "src/network/device_api.cpp").read_text()
        self.assertNotIn("continue_from_refill", api)
        self.assertIn("case GrindPhase::REFILL_CONFIRM:", api)


if __name__ == "__main__":
    unittest.main()
