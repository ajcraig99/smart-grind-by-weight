#pragma once

#include <Arduino.h>
#include <algorithm>
#include <atomic>
#include "../../config/constants.h"

/**
 * CircularBufferMath - Generic time-based mathematical operations on raw ADC data
 * 
 * This class provides time-based filtering and analysis operations on raw integer
 * data (typically ADC readings) using a circular buffer approach. It replaces the
 * weight-specific filter to provide:
 * 
 * - Full raw ADC resolution preservation (24-bit signed integers)
 * - Generic time-based mathematical operations
 * - Foundation for advanced filtering algorithms  
 * - Clear separation between raw data processing and unit conversion
 * 
 * Key Features:
 * - Large fixed circular buffer (no data loss during window changes)
 * - Time-based smoothing windows (millisecond-specified, not sample count)
 * - Outlier rejection using min/max removal
 * - Statistical analysis capabilities
 * - Ready for 10 SPS to 80+ SPS operation without code changes
 *
 * Threading: add_sample() is called only by the weight sampling task. Every
 * other method may be called from any task. The write index and sample count
 * are published together after the sample they expose, and a clear requested
 * by another task is applied by the next add_sample(), so readers always see a
 * consistent window and only the sampling task changes the buffer.
 */
class CircularBufferMath {
public:
    
    struct RawDataReading {
        int32_t raw_value;
        float confidence;        // 0.0-1.0 stability confidence  
        uint32_t settle_time_ms; // How long settling took
        bool timeout_occurred;   // True if settled due to timeout
    };
    
private:
    // Atomic fields: a slot is rewritten only when the buffer wraps, long
    // after any reader has finished with it, but the overlap stays defined.
    struct AdcSample {
        std::atomic<int32_t> raw_value{0};       // Raw signed ADC reading (e.g., 24-bit HX711)
        std::atomic<uint32_t> timestamp_ms{0};   // Sample timestamp
    };
    
    // Large fixed buffer - sized for 10+ seconds at 80 SPS = 800+ samples
    // Using 1024 for power-of-2 efficiency and future headroom
    static const uint16_t MAX_BUFFER_SIZE = 1024;
    
    // A settling decision needs at least this many samples, taken over at
    // least this long; 0-2 samples have no meaningful spread.
    static constexpr int MIN_SETTLING_SAMPLES = 3;
    static constexpr uint32_t MIN_SETTLING_WINDOW_MS = (HW_LOADCELL_SAMPLE_INTERVAL_MS * 7) / 2;

    AdcSample circular_buffer[MAX_BUFFER_SIZE];
    // (count << 16) | write index, published by add_sample() after the sample.
    std::atomic<uint32_t> published_indices_{0};
    // clear_all_samples() increments the request count; add_sample() applies
    // it and records the count it applied. Unequal counts read as empty.
    std::atomic<uint32_t> clear_requests_{0};
    std::atomic<uint32_t> clears_applied_{0};

    struct Snapshot {
        uint16_t write_index;
        uint16_t count;
    };
    Snapshot snapshot() const;
    
    // For asymmetric display filtering (fast up, slow down) on raw values.
    // Stepped from several tasks; each field is atomic, the filter is advisory.
    std::atomic<int32_t> display_filtered_raw_{0};
    std::atomic<bool> display_filter_initialized_{false};
    
    // Flow rate stability tracking (raw units per second)
    mutable uint32_t flow_stable_since_ms;  // When flow rate first became stable
    mutable bool flow_stability_initialized;
    
    // Copies the samples no older than window_ms, newest first, with their
    // ages in ms when ages_out is given. Ages keep window tests wrap-safe.
    int collect_window(uint32_t window_ms, int32_t* values_out, uint32_t* ages_out,
                       int capacity) const;
    int get_samples_in_window(uint32_t window_ms, int32_t* samples_out,
                              int samples_capacity) const;
    int32_t apply_outlier_rejection(const int32_t* samples, int count) const;
    float calculate_standard_deviation(const int32_t* samples, int count) const;
    int32_t get_latest_sample() const;
    int calculate_max_samples_for_window(uint32_t window_ms) const;
    
public:
    CircularBufferMath();
    
    // Core data input - called from LoadCell::sample_and_feed_filter()
    void add_sample(int32_t raw_adc_value, uint32_t timestamp_ms);
    
    // Unified smoothing method on raw data
    int32_t get_smoothed_raw(uint32_t window_ms) const;
    
    // Specialized raw readings with different time windows
    int32_t get_instant_raw() const;           // Latest single sample
    int32_t get_raw_low_latency() const;       // 50ms window - for real-time control  
    int32_t get_display_raw();                 // 250ms window + asymmetric filter - for UI
    int32_t get_raw_high_latency() const;      // 250ms window - for final measurements
    bool get_window_delta(uint32_t window_ms, int32_t* delta_out,
                          uint32_t* span_ms_out = nullptr, int* samples_out = nullptr) const;
    // Newest sample and its timestamp, read together; the timestamp identifies
    // the sample. Returns false when the buffer is empty.
    bool get_latest_sample(int32_t* raw_out, uint32_t* timestamp_out) const;
    
    // Raw access for diagnostics
    uint16_t get_sample_count() const { return snapshot().count; }
    uint32_t get_buffer_time_span_ms() const;
    
    // Settling analysis - window_ms based with raw value threshold. Needs at
    // least MIN_SETTLING_SAMPLES over max(window_ms, MIN_SETTLING_WINDOW_MS),
    // a standard deviation within the threshold, and end-to-end drift within
    // twice the threshold so a slow trickle does not read as settled.
    bool is_settled(uint32_t window_ms, int32_t threshold_raw_units) const;
    float get_settling_confidence(uint32_t window_ms) const;
    
    // Flow rate calculation in raw units per second with configurable time window
    float get_raw_flow_rate(uint32_t window_ms = 200) const;      // Default 200ms window
    float get_raw_flow_rate_95th_percentile(uint32_t window_ms = 200) const; // 95th percentile
    bool raw_flowrate_is_stable(uint32_t window_ms = 100) const;  // Check if flow rate stable
    
    // Statistical operations on raw data
    float get_standard_deviation_raw(uint32_t window_ms) const;
    int32_t get_min_raw(uint32_t window_ms) const;
    int32_t get_max_raw(uint32_t window_ms) const;
    
    // Reset functions. Safe from any task; the sampling task applies a clear
    // before its next sample, and until then the buffer reads as empty.
    void reset_display_filter();
    void clear_all_samples();
};
