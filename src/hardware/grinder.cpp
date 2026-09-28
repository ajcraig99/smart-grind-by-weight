#include "grinder.h"
#include "../controllers/grind_events.h"
#include "../config/constants.h"
#include <driver/gpio.h>
#include <algorithm>
#include <iterator>
#if DEBUG_ENABLE_LOADCELL_MOCK
#include "mock_hx711_driver.h"
#endif

namespace {
// A finite pulse whose completion interrupt never arrives is treated as a
// driver failure after its own length plus this margin.
constexpr uint32_t kPulseCompletionMarginMs = 500;
}

// The level is latched before the output is enabled, avoiding the pulled-up,
// undriven moment that gpio_reset_pin() creates. gpio_set_direction() also
// routes the plain GPIO signal to the pin, which detaches the RMT peripheral
// from it. Register writes only, so the dead-man timer may call this while
// RMT is in use.
void Grinder::hold_pin_low(int pin) {
    const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
    gpio_set_level(gpio, 0);
    gpio_pullup_dis(gpio);
    gpio_pulldown_en(gpio);
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
}

void Grinder::init(int pin) {
    if (initialized) return;
    motor_pin = pin;
    grinding = false;
    pulse_active = false;
    rmt_initialized = false;
    current_encoder = nullptr;
    motor_start_time = 0;

    // Initialize background indicator
    background_active = false;
    ui_event_callback = nullptr;

    hold_pin_low(motor_pin);

#if DEBUG_ENABLE_LOADCELL_MOCK
    initialized = true;
    return;
#endif
    
    // Initialize RMT for all motor control (both continuous and pulse)
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = (gpio_num_t)motor_pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, // 1MHz resolution = 1µs per tick
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    
    if (rmt_new_tx_channel(&tx_chan_config, &rmt_channel) != ESP_OK) return;
    rmt_tx_event_callbacks_t callbacks{};
    callbacks.on_trans_done = &Grinder::on_transmit_done;
    rmt_copy_encoder_config_t encoder_config{};
    if (rmt_tx_register_event_callbacks(rmt_channel, &callbacks, this) != ESP_OK ||
        rmt_new_copy_encoder(&encoder_config, &current_encoder) != ESP_OK) {
        rmt_del_channel(rmt_channel);
        return;
    }
    if (rmt_enable(rmt_channel) != ESP_OK) {
        rmt_del_encoder(current_encoder);
        current_encoder = nullptr;
        rmt_del_channel(rmt_channel);
        return;
    }

    // Without the dead-man a hung control loop could leave the motor running,
    // so refuse to drive the motor at all if it cannot be armed.
    esp_timer_create_args_t timer_args{};
    timer_args.callback = &Grinder::deadman_check;
    timer_args.arg = this;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "motor_deadman";
    timer_args.skip_unhandled_events = true;
    if (esp_timer_create(&timer_args, &deadman_timer_) != ESP_OK ||
        esp_timer_start_periodic(deadman_timer_,
                                 static_cast<uint64_t>(HW_MOTOR_DEADMAN_CHECK_INTERVAL_MS) * 1000ULL) != ESP_OK) {
        LOG_BLE("[Grinder] Dead-man timer unavailable; motor disabled\n");
        if (deadman_timer_) {
            esp_timer_delete(deadman_timer_);
            deadman_timer_ = nullptr;
        }
        rmt_disable(rmt_channel);
        rmt_del_encoder(current_encoder);
        current_encoder = nullptr;
        rmt_del_channel(rmt_channel);
        hold_pin_low(motor_pin);
        return;
    }
    rmt_initialized = true;
    initialized = true;
}

void Grinder::start() {
#if DEBUG_ENABLE_LOADCELL_MOCK
    if (!initialized) return;
    MockHX711Driver::notify_grinder_start();
    pulse_active = false;
    grinding = true;
    motor_start_time = millis();
    emit_background_change(true);
    return;
#endif
    if (!initialized || !rmt_initialized || safety_stop_.load()) return;

    // Stop the old transaction before modifying its payload or encoder state.
    stop();
    if (!initialized) return;
    motor_start_time = millis();
    
    // Use RMT infinite loop for continuous grinding
    symbols[0] = {};
    symbols[0].duration0 = 32767;
    symbols[0].level0 = 1;
    symbols[0].duration1 = 32767;
    symbols[0].level1 = 1;
    
    rmt_transmit_config_t tx_config = {
        .loop_count = -1, // Infinite loop
    };

    // Arm the dead-man before the output goes HIGH.
    keepalive_ms_.store(millis());
    continuous_active_.store(true);
    if (rmt_transmit(rmt_channel, current_encoder, symbols, sizeof(symbols[0]), &tx_config) != ESP_OK) {
        LOG_BLE("[Grinder] Failed to start continuous transmission\n");
        stop();
        return;
    }
    grinding = true;
    emit_background_change(true);
}

void Grinder::stop() {
#if DEBUG_ENABLE_LOADCELL_MOCK
    if (!initialized) return;
    MockHX711Driver::notify_grinder_stop();
    grinding = false;
    pulse_active = false;
    emit_background_change(false);
    return;
#endif
    if (!initialized || !rmt_initialized) return;
    
    // Stop RMT transmission (works for both infinite loop and finite pulses)
    if (rmt_disable(rmt_channel) != ESP_OK ||
        rmt_encoder_reset(current_encoder) != ESP_OK ||
        rmt_enable(rmt_channel) != ESP_OK) {
        // Disconnect RMT from the output and refuse further starts until reboot.
        // Keep its storage alive: a failed cancellation may still reference it.
        hold_pin_low(motor_pin);
        initialized = false;
        rmt_initialized = false;
        LOG_BLE("[Grinder] RMT reset failed; motor disabled until reboot\n");
    }
    
    continuous_active_.store(false);
    grinding = false;
    pulse_active = false;
    emit_background_change(false);
}

void Grinder::start_pulse_rmt(uint32_t duration_ms) {
#if DEBUG_ENABLE_LOADCELL_MOCK
    if (!initialized) return;
    MockHX711Driver::notify_pulse(duration_ms);
    pulse_active = true;
    grinding = true;
    motor_start_time = millis();
    emit_background_change(true);
    return;
#endif
    if (!initialized || !rmt_initialized || safety_stop_.load()) return;

    stop();
    if (!initialized) return;
    // Reserve one half-symbol for LOW. Covers every supported call, including
    // the one-second motor test, without multiplication overflow or allocation.
    constexpr uint32_t max_duration_ms = (kSymbolCount * 2 - 1) * 32767U / 1000U;
    if (duration_ms == 0 || duration_ms > max_duration_ms) {
        LOG_BLE("[Grinder] Pulse duration outside supported range\n");
        return;
    }
    std::fill(std::begin(symbols), std::end(symbols), rmt_symbol_word_t{});
    uint32_t remaining = duration_ms * 1000U;
    size_t halves = 0;
    while (remaining > 0) {
        const uint32_t ticks = std::min<uint32_t>(remaining, 32767U);
        auto& symbol = symbols[halves / 2];
        if (halves % 2 == 0) {
            symbol.duration0 = ticks;
            symbol.level0 = 1;
        } else {
            symbol.duration1 = ticks;
            symbol.level1 = 1;
        }
        remaining -= ticks;
        ++halves;
    }
    // All HIGH halves are consecutive. Only the final half is LOW.
    if (halves % 2 == 0) symbols[halves / 2].duration0 = 1;
    else symbols[halves / 2].duration1 = 1;
    ++halves;
    rmt_transmit_config_t tx_config{}; // no repeats, end output LOW
    pulse_done_.store(false);
    if (rmt_transmit(rmt_channel, current_encoder, symbols,
                     ((halves + 1) / 2) * sizeof(symbols[0]), &tx_config) != ESP_OK) {
        LOG_BLE("[Grinder] Failed to start pulse transmission\n");
        stop();
        return;
    }
    motor_start_time = millis();
    pulse_deadline_ms_ = motor_start_time + duration_ms + kPulseCompletionMarginMs;
    pulse_active = true;
    grinding = true;
    emit_background_change(true);
}

bool Grinder::is_pulse_complete() {
#if DEBUG_ENABLE_LOADCELL_MOCK
    if (!pulse_active) return true;
    if (!MockHX711Driver::is_pulse_active()) {
        pulse_active = false;
        grinding = false;
        emit_background_change(false);
        return true;
    }
    return false;
#endif
    if (!pulse_active) return true;
    
    // A queued transmission can still have a LOW GPIO before it starts, so
    // completion comes from the driver's interrupt, not the pin level. (Polling
    // rmt_tx_wait_all_done() with no timeout logs a driver error every call.)
    if (pulse_done_.load()) {
        pulse_active = false;
        grinding = false;
        emit_background_change(false);
        return true;
    }
    if (static_cast<int32_t>(millis() - pulse_deadline_ms_) >= 0) {
        LOG_BLE("[Grinder] Pulse completion was not reported; stopping motor\n");
        stop();
        return true;
    }
    
    return false;
}

void Grinder::keep_alive() {
    keepalive_ms_.store(millis());
}

bool IRAM_ATTR Grinder::on_transmit_done(rmt_channel_handle_t, const rmt_tx_done_event_data_t*,
                                         void* context) {
    static_cast<Grinder*>(context)->pulse_done_.store(true);
    return false;  // No task was woken.
}

void Grinder::deadman_check(void* context) {
    auto* self = static_cast<Grinder*>(context);
    if (!self->continuous_active_.load() || self->safety_stop_.load()) return;
    if (static_cast<uint32_t>(millis() - self->keepalive_ms_.load()) < HW_MOTOR_DEADMAN_TIMEOUT_MS) return;
    // The control loop that owns this run has stopped responding. Latch first
    // so no new start can race the forced stop, then cut the output.
    self->safety_stop_.store(true);
    hold_pin_low(self->motor_pin);
}

bool Grinder::is_motor_settled() const {
    // Return true if sufficient time has passed since motor start
    if (motor_start_time == 0) {
        return false;  // Motor has never started
    }
    return (millis() - motor_start_time) >= HW_GRINDER_SETTLING_TIME_MS;
}

void Grinder::set_ui_event_callback(const std::function<void(const GrindEventData&)>& callback) {
    ui_event_callback = callback;
}

void Grinder::emit_background_change(bool active) {
    if (background_active == active) {
        return; // No change
    }
    
    background_active = active;
    
    if (ui_event_callback) {
        // Properly initialize all required fields to prevent null pointer crashes
        GrindEventData event_data = {};
        event_data.event = UIGrindEvent::BACKGROUND_CHANGE;
        event_data.phase = GrindPhase::IDLE;  // Safe default
        event_data.current_weight = 0.0f;
        event_data.progress_percent = 0;
        event_data.phase_display_text = "BACKGROUND";  // Safe string for logging
        event_data.show_taring_text = false;
        event_data.background_active = active;
        
        ui_event_callback(event_data);
        
        LOG_BLE("[Grinder] Background change: %s\n", active ? "ACTIVE" : "INACTIVE");
    }
}
