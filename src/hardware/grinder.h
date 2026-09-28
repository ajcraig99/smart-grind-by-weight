#pragma once
#include <Arduino.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include <esp_timer.h>
#include <atomic>
#include <functional>
#include "../config/constants.h"

// Forward declarations
struct GrindEventData;
enum class UIGrindEvent;

class Grinder {
private:
    int motor_pin;
    bool grinding;
    bool initialized = false;

    // RMT pulse control
    rmt_channel_handle_t rmt_channel;
    rmt_encoder_handle_t current_encoder;
    bool pulse_active;
    bool rmt_initialized;
    // The asynchronous encoder reads this storage until completion or stop.
    static constexpr size_t kSymbolCount = 32;
    rmt_symbol_word_t symbols[kSymbolCount]{};
    // Set from the RMT interrupt when a finite pulse has been sent.
    std::atomic<bool> pulse_done_{false};
    unsigned long pulse_deadline_ms_ = 0;

    // Dead-man protection for continuous runs, checked by an esp_timer.
    esp_timer_handle_t deadman_timer_ = nullptr;
    std::atomic<bool> continuous_active_{false};
    std::atomic<uint32_t> keepalive_ms_{0};
    std::atomic<bool> safety_stop_{false};

    // Motor settling tracking
    unsigned long motor_start_time;

    // Background indicator state (always compiled in)
    bool background_active;
    std::function<void(const GrindEventData&)> ui_event_callback;

    void emit_background_change(bool active);
    static bool on_transmit_done(rmt_channel_handle_t channel, const rmt_tx_done_event_data_t* event,
                                 void* context);
    static void deadman_check(void* context);

public:
    // Drives a motor output LOW as a plain GPIO. Needs no init(), so boot
    // calls it before anything else to keep the pin from floating while the
    // filesystem and display start.
    static void hold_pin_low(int pin);

    void init(int pin);
    void start();
    void stop();

    // RMT-based precise pulse control
    void start_pulse_rmt(uint32_t duration_ms);
    bool is_pulse_complete();

    // Called by the grind control loop every cycle. A continuous run without
    // a keep-alive for HW_MOTOR_DEADMAN_TIMEOUT_MS is stopped by the dead-man.
    void keep_alive();
    // True once the dead-man has stopped the motor; starts are then refused
    // until reboot.
    bool has_safety_stop() const { return safety_stop_.load(); }

    bool is_grinding() const { return grinding; }
    bool is_initialized() const { return initialized; }
    bool is_motor_settled() const;

    // Background indicator setup (always compiled in)
    void set_ui_event_callback(const std::function<void(const GrindEventData&)>& callback);
};
