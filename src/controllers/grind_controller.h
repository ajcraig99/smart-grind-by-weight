#pragma once
#include "../config/constants.h"
#include "../hardware/WeightSensor.h"
#include "../hardware/grinder.h"
#include "../logging/grind_logging.h"
#include "grind_mode.h"
#include "grind_ui_events.h"
#include "grind_session.h"
#include "grind_session_result.h"
#include "grind_strategy.h"
#include "net_weight_guard.h"
#include "weight_grind_strategy.h"
#include "time_grind_strategy.h"
#include "../system/operation_interlock.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class DiagnosticsController;

// Flash operation request structure for Core 0 → Core 1 communication
struct FlashOpRequest {
    enum Type {
        START_GRIND_SESSION,
        END_GRIND_SESSION,
        UPDATE_MANUAL_RUNTIME
    };
    
    Type operation_type;
    GrindSessionDescriptor descriptor; // For START_GRIND_SESSION
    char result_string[32];  // "COMPLETE", "TIMEOUT", "OVERSHOOT", etc. (for END_GRIND_SESSION)
    float start_weight;      // For START_GRIND_SESSION (pre-tare snapshot)
    float final_weight;      // For END_GRIND_SESSION
    uint8_t pulse_count;     // For END_GRIND_SESSION
    uint8_t refill_count;    // For END_GRIND_SESSION: resumes after running out of beans
    uint32_t completed_at_ms; // Absolute clock at terminal phase entry, not save time
    uint32_t motor_runtime_ms; // For UPDATE_MANUAL_RUNTIME
};

// Log message structure for Core 0 → Core 1 communication
struct LogMessage {
    char message[128];  // Pre-formatted log message
};

// Calculated values for a single update cycle - passed to methods to avoid redundant calculations
struct GrindLoopData {
    float current_weight;       // For control logic (low_latency)
    float display_weight;       // For UI display (always calculated)
    uint32_t timestamp_ms;
    float weight_delta;
    float flow_rate;
    uint8_t motor_is_on;
    uint8_t phase_id;
    unsigned long now;
};

struct PulseReport {
    float start_weight;
    float end_weight;
    float duration_ms;
};

// Outcome of CONTINUE on the purge prompt.
enum class PurgeContinueResult {
    CONTINUED,        // Grinding on; re-tares first if the cup was emptied or swapped
    VESSEL_MISSING,   // Still waiting: the scale reads as if the cup were off
    SCALE_NOT_READY,  // Still waiting: no fresh scale reading
    NOT_WAITING       // The session has already left the purge prompt
};

// Outcome of CONTINUE on the refill prompt. A press on an unsettled scale
// returns WAITING_FOR_SETTLE; the control loop then finishes it and leaves
// the outcome for take_refill_outcome().
enum class RefillContinueResult {
    NONE,                // Nothing new to report
    CONTINUED,           // Grinding on from the existing zero
    WAITING_FOR_SETTLE,  // Motor off until the reading settles, at most GRIND_REFILL_SETTLE_TIMEOUT_MS
    VESSEL_MISSING,      // Still waiting: the settled reading looks as if the cup were off
    READING_MOVED,       // Still waiting: the settled reading moved more than GRIND_REFILL_MOVED_THRESHOLD_G
    SETTLE_TIMEOUT,      // Still waiting: the reading never settled, so CONTINUE was dropped
    CUP_LIFTED,          // Still waiting: the cup was lifted while waiting to settle, so CONTINUE was dropped
    SCALE_NOT_READY,     // Still waiting: no fresh scale reading
    NOT_WAITING          // The session has already left the refill prompt
};

// What the refill prompt shows.
struct RefillPromptInfo {
    bool no_beans_at_start;  // Nothing was ground yet: the hopper was empty from the start
    bool waiting_for_settle; // CONTINUE is pending a settled reading
    float pause_weight_g;    // Reading when grinding stopped (settled once the scale allowed)
    float target_weight_g;
};



// Controls the grinding process with predictive weight stopping and precision pulse corrections
class GrindController {
private:
    // Serialize complete state transitions, not just individual field writes.
    // Recursive because strategies/getters call back into the same controller.
    mutable std::recursive_mutex control_mutex_;
    OperationInterlock::Token operation_token_ = 0;
    friend class WeightGrindStrategy;
    friend class TimeGrindStrategy;

    WeightSensor* weight_sensor;
    Grinder* grinder;
    Preferences* preferences;
    float target_weight;
    uint32_t target_time_ms;
    GrindPhase phase;
    unsigned long start_time;
    unsigned long phase_start_time;
    unsigned long time_grind_start_ms;
    
    float tolerance;
    GrindMode mode;
    GrinderPurgeMode grinder_purge_mode_for_session;
    float grinder_purge_amount_g_for_session;

    // Timeout tracking
    GrindPhase timeout_phase;   // Phase when timeout occurred
    unsigned long timeout_pause_start;  // When we entered a paused state (PURGE_CONFIRM)
    unsigned long timeout_offset_ms;    // Accumulated time in paused states to exclude from timeout
    
    int pulse_attempts;
    unsigned long pulse_start_time;
    float current_pulse_duration_ms;
    
    
    
    float predictive_end_weight;
    volatile float grind_latency_ms;        // Thread-safe for Core 0 access
    PulseReport pulse_history[GRIND_MAX_PULSE_ATTEMPTS];
    volatile float motor_stop_target_weight; // Thread-safe for Core 0 access
    float final_weight; // Stores the final settled weight from final_measurement()

    // Flow detection confirmation variables
    bool flow_start_confirmed;
    // Dynamic pulse algorithm variables
    volatile float pulse_flow_rate;    // Thread-safe for Core 0 access
    
    // Loop counter variables for performance tracking
    volatile uint16_t current_phase_loop_count;  // Thread-safe for Core 0 access

    // Logging support
    uint8_t current_profile_id;
    GrindEvent event_in_progress; // Used to build data for the current phase event
    
    // State tracking for measurement calculations (eliminates calculations in logger)
    float last_logged_weight;       // Previous weight for delta calculation
    unsigned long last_logged_time; // Previous timestamp for relative timing
    bool force_measurement_log;     // Flag to force measurement logging on next update cycle

    // UI event system - thread-safe Core 0 → Core 1 communication
    GrindUIEventMailbox ui_events_;
    portMUX_TYPE ui_event_lock_ = portMUX_INITIALIZER_UNLOCKED;
    
    bool control_loop_paused_;      // Indicates control loop is suspended (e.g., purge confirmation)

    // Time mode pause state
    bool grind_paused_;
    uint32_t pause_start_ms_;
    uint32_t total_pause_ms_;
    
    // Flash operation queue - thread-safe Core 0 → Core 1 communication
    QueueHandle_t flash_op_queue;
    static const int FLASH_OP_QUEUE_SIZE = 5;
    
    // Log message queue - thread-safe Core 0 → Core 1 communication
    QueueHandle_t log_queue;
    static const int LOG_QUEUE_SIZE = 20;
    
    // Time mode pulse tracking
    int additional_pulse_count;
    uint32_t pulse_duration_ms;
    
    void (*ui_event_callback)(const GrindEventData&) = nullptr;
    bool ui_ready_for_setup = false; // Flag to track UI acknowledgment of INITIALIZING phase
    
    // Flag to prevent repeated flash operations for terminal phases (COMPLETED/TIMEOUT)
    bool session_end_flash_queued = false;
    char last_error_message[32];

    GrindSessionDescriptor session_descriptor;
    GrindStrategyContext strategy_context;
    IGrindStrategy* active_strategy = nullptr;
    WeightGrindStrategy weight_strategy;
    TimeGrindStrategy time_strategy;

    // Mechanical instability tracking
    int mechanical_anomaly_count_ = 0;
    unsigned long last_mechanical_event_ms_ = 0;
    float last_mechanical_weight_ = 0.0f;
    bool mechanical_monitor_initialized_ = false;

    // Detect actual cup/portafilter removal without reacting to isolated
    // negative load-cell spikes.
    NetWeightRemovalGuard net_weight_removal_guard_;
    uint32_t last_guard_sample_ms_ = 0;   // Timestamp of the sample the guard last counted
    float pre_final_settled_weight_ = 0;  // Settled weight that led to FINAL_SETTLING
    // Start of the settling before a pulse decision; the settling timeout
    // runs from here across PULSE_SETTLING and PULSE_DECISION.
    unsigned long pulse_settling_start_ms_ = 0;

    // Dry-run detection: weight and time of the last GRIND_DRY_RUN_MIN_PROGRESS_G gain
    float dry_run_reference_weight_ = 0;
    unsigned long dry_run_reference_ms_ = 0;

    // A re-tare after the purge prompt resumes in PREDICTIVE, not PRIME.
    bool resume_after_purge_ = false;
    // Set at start: this session stops at the purge prompt after priming.
    bool purge_prompt_due_ = false;
    // Settled reading when the purge prompt appeared, and whether the vessel
    // has been lifted since. Either change means the cup was emptied or
    // swapped, so CONTINUE re-tares; otherwise kept grounds count as dose.
    float post_purge_weight_ = 0.0f;
    bool vessel_lifted_since_purge_ = false;

    // Out of beans. A dry run pauses at REFILL_CONFIRM with the motor off;
    // CONTINUE resumes in refill_resume_phase_ from the existing zero.
    uint8_t refill_resume_count_ = 0;          // Resumes so far in this grind
    GrindPhase refill_resume_phase_ = GrindPhase::PREDICTIVE;
    bool refill_no_beans_at_start_ = false;    // Prompt reached before anything was ground
    float refill_pause_weight_ = 0.0f;         // Reference reading for the moved-cup check
    bool refill_reference_settled_ = false;    // refill_pause_weight_ is a settled reading
    bool refill_vessel_lifted_ = false;        // Cup lifted at some point during this pause
    bool refill_continue_pending_ = false;     // CONTINUE waits for a settled reading
    unsigned long refill_continue_requested_ms_ = 0;
    RefillContinueResult refill_outcome_ = RefillContinueResult::NONE;  // Result of a pending CONTINUE
    // The current stretch began with a resume, from this weight. A resumed
    // stretch that gains nothing points to a jam or open relay, not beans.
    bool refill_leg_resumed_ = false;
    float refill_leg_start_weight_ = 0.0f;
    int session_pulse_total_ = 0;              // Pulses of earlier stretches; pulse_attempts counts this one
    // Largest PREDICTIVE stop offset computed from flow in the sane band. A
    // resumed stretch is often too short to measure its own, so it starts from this.
    float peak_healthy_stop_offset_g_ = 0.0f;
    float peak_healthy_flow_gps_ = 0.0f;       // Largest PREDICTIVE flow in the sane band, for resumed pulses

    DiagnosticsController* diagnostics_controller_ = nullptr;

    // Motor response latency - runtime configurable
    float motor_response_latency_ms;

    // Coast ratio - runtime configurable
    float coast_ratio_;

    // Grind freshness tracking
    bool grinder_purged_since_boot;      // Tracks if grinder has been used since boot (RAM only)
    uint64_t last_purge_runtime_ms;      // Runtime when last grind completed (persisted)

public:
    using GrindSessionResult = ::GrindSessionResult;

private:
    GrindSessionResult last_session_result_ = GrindSessionResult::UNKNOWN;

public:
    // For compound callers and task suspension. Never hold this while waiting
    // for a UI acknowledgement or invoking UI callbacks.
    std::unique_lock<std::recursive_mutex> lock_control() const {
        return std::unique_lock<std::recursive_mutex>(control_mutex_);
    }

    void init(WeightSensor* lc, Grinder* gr, Preferences* prefs);
    bool start_grind(float target_weight, uint32_t target_time_ms, GrindMode grind_mode);
    void user_tare_request();
    void return_to_idle(); // Called by UI to acknowledge completion/timeout
    void stop_grind();
    // Called by UI to continue from PURGE_CONFIRM into PREDICTIVE, re-taring
    // first if the cup was emptied or swapped. With check_vessel set it keeps
    // waiting while the scale reads as if the vessel were still off.
    PurgeContinueResult continue_from_purge(bool check_vessel = true);
    // Called by UI for CONTINUE on the refill prompt. Compares a settled
    // reading with the one at the pause, waiting for the scale to settle if
    // needed. accept_current_reading skips the cup checks after the user has
    // confirmed the reading in a "Cup missing?" or "Cup moved?" dialog.
    RefillContinueResult continue_from_refill(bool accept_current_reading = false);
    // STOP on the refill prompt: ends the grind as "No beans?". False if not waiting.
    bool decline_refill();
    // Result of a CONTINUE the control loop finished; NONE if nothing new.
    RefillContinueResult take_refill_outcome();
    RefillPromptInfo get_refill_prompt_info() const;
    void update(); // Core 0 main control method - runs at fixed RTOS interval
    
    // Time mode pulse functionality
    void start_additional_pulse(); // Start an additional 100ms pulse in time mode
    bool can_pulse() const; // Check if additional pulses are allowed
    int get_additional_pulse_count() const { const auto control_lock = lock_control(); return additional_pulse_count; }

    // Time mode pause/resume
    void pause_grind();
    void resume_grind();
    bool is_grind_paused() const { const auto control_lock = lock_control(); return grind_paused_; }
    
    // UI event system
    void set_ui_event_callback(void (*callback)(const GrindEventData&));
    GrindSessionResult get_last_session_result() const { const auto control_lock = lock_control(); return last_session_result_; }
    void ui_acknowledge_phase_transition(); // Called by UI to confirm phase transition
    void process_queued_ui_events(); // Core 1: Consume latest display snapshots
    
    // Flash operation system
    void process_queued_flash_operations(); // Core 1: Process flash ops from Core 0 queue
    bool queue_flash_operation(const FlashOpRequest& request); // Core 0: Queue flash operation
    
    // Log message system
    void process_queued_log_messages(); // Core 1: Process log messages from Core 0 queue
    void queue_log_message(const char* format, ...); // Core 0: Queue formatted log message
    
    bool is_active() const;
    bool is_control_loop_paused() const { const auto control_lock = lock_control(); return control_loop_paused_; }
    GrindPhase get_phase() const { const auto control_lock = lock_control(); return phase; }
    const char* get_current_phase_name() const { const auto control_lock = lock_control(); return get_phase_name(phase); }
    int get_current_progress_percent() const { const auto control_lock = lock_control(); return get_progress_percent(); }
    float get_target_weight() const { const auto control_lock = lock_control(); return target_weight; }
    uint32_t get_target_time_ms() const { const auto control_lock = lock_control(); return target_time_ms; }
    uint32_t get_elapsed_grind_ms() const;
    static constexpr const char* PREF_KEY_PRIME_ENABLED = "prime_enabled";
    static constexpr const char* PREF_KEY_GRINDER_MODE = "grinder_mode";
    // ESP32 NVS keys are limited to 15 characters. The previous
    // "grinder_amount_g" key was 16 characters, so Preferences::putFloat()
    // rejected every purge-amount save.
    static constexpr const char* PREF_KEY_GRINDER_AMOUNT_G = "purge_amount_g";
    static_assert(sizeof("purge_amount_g") - 1 <= 15, "NVS preference key is too long");
    static constexpr const char* PREF_KEY_GRIND_FRESHNESS_HOURS = "freshness_hrs";
    static constexpr const char* PREF_KEY_COAST_RATIO = "coast_ratio";
    static constexpr const char* PREF_KEY_LAST_GRIND_RUNTIME = "last_grind_ms";
    GrindMode get_mode() const { const auto control_lock = lock_control(); return mode; }
    GrindSessionDescriptor get_session_descriptor() const { const auto control_lock = lock_control(); return session_descriptor; }
    
    // Grind logging functions
    void set_grind_profile_id(uint8_t profile_id) { const auto control_lock = lock_control(); if (phase != GrindPhase::IDLE) return; current_profile_id = profile_id; session_descriptor.profile_id = profile_id; }
    void send_measurements_data();           // Send structured measurement data via serial
    
    // Getter methods for logger (to eliminate calculations in logger)
    float get_current_flow_rate() const;
    float get_motor_stop_target_weight() const { const auto control_lock = lock_control(); return motor_stop_target_weight; }
    float get_grind_latency_ms() const { const auto control_lock = lock_control(); return grind_latency_ms; }
    float get_last_logged_weight() const { const auto control_lock = lock_control(); return last_logged_weight; }
    void set_last_logged_weight(float weight) { const auto control_lock = lock_control(); last_logged_weight = weight; } // Thread-safe setter

    int get_mechanical_anomaly_count() const { const auto control_lock = lock_control(); return mechanical_anomaly_count_; }
    void reset_mechanical_anomaly_count();

    void set_diagnostics_controller(DiagnosticsController* diagnostics) { const auto control_lock = lock_control(); diagnostics_controller_ = diagnostics; }

    // Motor response latency accessors
    float get_motor_response_latency() const { const auto control_lock = lock_control(); return motor_response_latency_ms; }
    void set_motor_response_latency(float value);
    float get_min_pulse_duration() const { const auto control_lock = lock_control(); return motor_response_latency_ms; }
    float get_max_pulse_duration() const { const auto control_lock = lock_control(); return motor_response_latency_ms + GRIND_MOTOR_MAX_PULSE_DURATION_MS; }
    void load_motor_latency();
    bool save_motor_latency(float value);

    // Coast ratio accessors
    float get_coast_ratio() const { const auto control_lock = lock_control(); return coast_ratio_; }
    void set_coast_ratio(float value);
    void load_coast_ratio();
    bool save_coast_ratio(float value);

    // Grind freshness accessors
    bool get_grinder_purged_since_boot() const { const auto control_lock = lock_control(); return grinder_purged_since_boot; }
    uint64_t get_last_purge_runtime_ms() const { const auto control_lock = lock_control(); return last_purge_runtime_ms; }
    
    // Removed - predictive logic now inline in update_realtime()
    
    
private:
    bool queue_terminal_session();
    void switch_phase(GrindPhase new_phase, const GrindLoopData& loop_data = {});
    void final_measurement(const GrindLoopData& loop_data);
    void finish_weight_grind(float measured_weight, const GrindLoopData& loop_data);
    void monitor_mechanical_instability(const GrindLoopData& loop_data);

    bool check_timeout() const;
    bool grounds_are_stale() const;
    bool vessel_removal_confirmed(float* sample_weight);
    bool dry_run_detected(const GrindLoopData& loop_data);
    void end_or_pause_dry_run(const GrindLoopData& loop_data);
    void enter_refill_pause(const GrindLoopData& loop_data);
    void refill_pause_tick(const GrindLoopData& loop_data, bool vessel_lifted);
    RefillContinueResult evaluate_refill_continue(float settled_weight, const GrindLoopData& loop_data);
    void resume_after_refill(const GrindLoopData& loop_data);
    void abort_session(GrindSessionResult result, const char* message, const GrindLoopData& loop_data);
    uint8_t get_current_phase_id() const;
    
    // UI event emission - thread-safe for Core 0
    void emit_ui_event(const GrindEventData& data);
    void emit_progress_update(const GrindLoopData& loop_data);
    
    // Core 0 control methods  
    bool should_log_measurements() const;
    
    // Internal state methods (moved from public to prevent polling)
    bool show_taring_text() const { return phase == GrindPhase::INITIALIZING || phase == GrindPhase::SETUP || phase == GrindPhase::TARING || phase == GrindPhase::TARE_CONFIRM; }
    bool is_completed() const { return phase == GrindPhase::COMPLETED; }
    bool is_timeout() const { return phase == GrindPhase::TIMEOUT; }
    int get_progress_percent() const;
    float get_grind_time() const;
    GrindPhase get_timeout_phase() const { return timeout_phase; }
    const char* get_phase_name(GrindPhase p = static_cast<GrindPhase>(-1)) const;

    void set_error_message(const char* message);
};
