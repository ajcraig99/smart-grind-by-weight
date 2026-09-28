#include "circular_buffer_math.h"
#include "../../config/constants.h"
#include <math.h>
#include <algorithm>

CircularBufferMath::CircularBufferMath() {
    flow_stable_since_ms = 0;
    flow_stability_initialized = false;
}
    
CircularBufferMath::Snapshot CircularBufferMath::snapshot() const {
    // A requested clear is visible at once, before the sampling task applies it.
    const uint32_t requested = clear_requests_.load(std::memory_order_acquire);
    if (clears_applied_.load(std::memory_order_acquire) != requested) {
        return {0, 0};
    }
    const uint32_t packed = published_indices_.load(std::memory_order_acquire);
    return {static_cast<uint16_t>(packed & 0xFFFFu), static_cast<uint16_t>(packed >> 16)};
}

void CircularBufferMath::add_sample(int32_t raw_adc_value, uint32_t timestamp_ms) {
    // Raw ADC values should be valid 24-bit signed integers
    // We don't validate range here as different ADCs have different ranges
    
    // Only the sampling task gets here, so it alone moves the indices.
    const uint32_t requested = clear_requests_.load(std::memory_order_acquire);
    const uint32_t packed = published_indices_.load(std::memory_order_relaxed);
    uint16_t write_index = static_cast<uint16_t>(packed & 0xFFFFu);
    uint16_t samples_count = static_cast<uint16_t>(packed >> 16);
    if (requested != clears_applied_.load(std::memory_order_relaxed)) {
        // Keep the write position: restarting at slot 0 would overwrite
        // samples that a reader holding an older snapshot may be reading.
        samples_count = 0;
        flow_stability_initialized = false;
    }

    // Add raw value directly to circular buffer (no IIR filtering)
    circular_buffer[write_index].raw_value.store(raw_adc_value, std::memory_order_relaxed);
    circular_buffer[write_index].timestamp_ms.store(timestamp_ms, std::memory_order_relaxed);
    
    // Advance write index (circular)
    write_index = (write_index + 1) % MAX_BUFFER_SIZE;
    
    // Track sample count (up to buffer size)
    if (samples_count < MAX_BUFFER_SIZE) {
        samples_count++;
    }

    // Publish the sample before the clear it replaces is marked applied, so a
    // reader sees either an empty buffer or the new window, never the old one.
    published_indices_.store((static_cast<uint32_t>(samples_count) << 16) | write_index,
                             std::memory_order_release);
    clears_applied_.store(requested, std::memory_order_release);
}

int32_t CircularBufferMath::get_instant_raw() const {
    // Return most recent sample
    return get_latest_sample();
}

int32_t CircularBufferMath::get_latest_sample() const {
    int32_t raw = 0;
    uint32_t timestamp = 0;
    return get_latest_sample(&raw, &timestamp) ? raw : 0;
}

bool CircularBufferMath::get_latest_sample(int32_t* raw_out, uint32_t* timestamp_out) const {
    const Snapshot at = snapshot();
    if (at.count == 0) return false;
    
    // Most recent sample is at (write_index - 1) % MAX_BUFFER_SIZE
    const AdcSample& latest = circular_buffer[(at.write_index - 1 + MAX_BUFFER_SIZE) % MAX_BUFFER_SIZE];
    if (raw_out) *raw_out = latest.raw_value.load();
    if (timestamp_out) *timestamp_out = latest.timestamp_ms.load();
    return true;
}

// Unified smoothing method with outlier rejection on raw data
int32_t CircularBufferMath::get_smoothed_raw(uint32_t window_ms) const {
    // Calculate max samples needed for this window
    int max_samples = calculate_max_samples_for_window(window_ms);
    if (max_samples == 0) return 0;
    
    // Allocate temporary array on stack (reasonable size expected)
    int32_t* samples = (int32_t*)alloca(max_samples * sizeof(int32_t));
    
    // Get samples within time window
    int actual_samples = get_samples_in_window(window_ms, samples, max_samples);
    
    if (actual_samples == 0) {
        return get_latest_sample(); // Fallback to latest sample
    }
    
    // Apply outlier rejection and return smoothed result
    return apply_outlier_rejection(samples, actual_samples);
}

int CircularBufferMath::collect_window(uint32_t window_ms, int32_t* values_out,
                                       uint32_t* ages_out, int capacity) const {
    if (!values_out || capacity <= 0) return 0;
    
    const Snapshot at = snapshot();
    // Read the clock after the snapshot: every published sample was
    // timestamped before it was published, so no age can be negative.
    const uint32_t current_time = millis();
    int collected_samples = 0;
    
    // Walk backwards from most recent sample
    for (int i = 0; i < at.count && collected_samples < capacity; i++) {
        uint16_t index = (at.write_index - 1 - i + MAX_BUFFER_SIZE) % MAX_BUFFER_SIZE;
        const uint32_t age_ms = current_time - circular_buffer[index].timestamp_ms.load();
        
        // Check if sample is within time window
        if (age_ms <= window_ms) {
            values_out[collected_samples] = circular_buffer[index].raw_value.load();
            if (ages_out) ages_out[collected_samples] = age_ms;
            collected_samples++;
        } else {
            break; // Samples are time-ordered, so we can stop here
        }
    }
    
    return collected_samples;
}

int CircularBufferMath::get_samples_in_window(uint32_t window_ms, int32_t* samples_out,
                                              int samples_capacity) const {
    return collect_window(window_ms, samples_out, nullptr, samples_capacity);
}

int32_t CircularBufferMath::apply_outlier_rejection(const int32_t* samples, int count) const {
    if (count <= 0) return 0;
    if (count == 1) return samples[0];
    if (count == 2) return (samples[0] + samples[1]) / 2;

    // Decide how many to reject from each side
    int reject_each_side = 1; // (HW_LOADCELL_SAMPLE_RATE_SPS == 80) ? 8 : 1;

    // Not enough samples left -> fallback to median
    if (count <= 2 * reject_each_side) {
        int32_t* sorted_samples = (int32_t*)alloca(count * sizeof(int32_t));
        memcpy(sorted_samples, samples, count * sizeof(int32_t));
        std::sort(sorted_samples, sorted_samples + count);
        return sorted_samples[count / 2];
    }

    // Copy and sort
    int32_t* sorted_samples = (int32_t*)alloca(count * sizeof(int32_t));
    memcpy(sorted_samples, samples, count * sizeof(int32_t));
    std::sort(sorted_samples, sorted_samples + count);

    // Average trimmed values
    int samples_to_average = count - 2 * reject_each_side;
    int64_t sum = 0;
    for (int i = reject_each_side; i < count - reject_each_side; i++) {
        sum += sorted_samples[i];
    }

    return static_cast<int32_t>(sum / samples_to_average);
}

int CircularBufferMath::calculate_max_samples_for_window(uint32_t window_ms) const {
    // Estimate max samples
    int estimated_samples = (window_ms * HW_LOADCELL_SAMPLE_RATE_SPS) / 1000 + 10; // +10 for safety margin
    
    // Cap at reasonable limits
    const int samples_count = snapshot().count;
    if (estimated_samples > samples_count) {
        estimated_samples = samples_count;
    }
    if (estimated_samples > MAX_BUFFER_SIZE) {
        estimated_samples = MAX_BUFFER_SIZE;
    }
    
    return estimated_samples;
}

int32_t CircularBufferMath::get_raw_low_latency() const {
    return get_smoothed_raw(100); // 100ms window for real-time control
}

int32_t CircularBufferMath::get_display_raw() {
    // Asymmetric display filter on raw values (fast up, slow down)
    int32_t current_raw = get_smoothed_raw(300); // 300ms base window
    
    if (!display_filter_initialized_.load()) {
        display_filtered_raw_.store(current_raw);
        display_filter_initialized_.store(true);
        return current_raw;
    }
    
    // Apply asymmetric filter (fast up, slow down) - adapted for raw values
    // Use deadband equivalent to ~0.01g in raw units (approximate)
    int32_t raw_deadband = 100; // This should be configurable based on calibration
    int32_t display_filtered_raw = display_filtered_raw_.load();
    
    if (abs(current_raw - display_filtered_raw) < raw_deadband) {
        return display_filtered_raw; // No change within deadband
    }
    
    if (current_raw > display_filtered_raw) {
        // Fast response for increases
        display_filtered_raw = current_raw;
    } else {
        // Slow response for decreases
        float alpha = SYS_DISPLAY_FILTER_ALPHA_DOWN; // From constants.h
        display_filtered_raw = (int32_t)(alpha * current_raw + (1.0f - alpha) * display_filtered_raw);
    }
    
    display_filtered_raw_.store(display_filtered_raw);
    return display_filtered_raw;
}

int32_t CircularBufferMath::get_raw_high_latency() const {
    return get_smoothed_raw(300); // 300ms window for final measurements
}

uint32_t CircularBufferMath::get_buffer_time_span_ms() const {
    const Snapshot at = snapshot();
    if (at.count < 2) return 0;
    
    // Time span from oldest to newest sample
    uint16_t oldest_index = (at.write_index - at.count + MAX_BUFFER_SIZE) % MAX_BUFFER_SIZE;
    uint16_t newest_index = (at.write_index - 1 + MAX_BUFFER_SIZE) % MAX_BUFFER_SIZE;
    
    return circular_buffer[newest_index].timestamp_ms.load() - circular_buffer[oldest_index].timestamp_ms.load();
}

bool CircularBufferMath::get_window_delta(uint32_t window_ms, int32_t* delta_out,
                                          uint32_t* span_ms_out, int* samples_out) const {
    const Snapshot at = snapshot();
    if (!delta_out || at.count < 2) {
        if (delta_out) {
            *delta_out = 0;
        }
        if (span_ms_out) {
            *span_ms_out = 0;
        }
        if (samples_out) {
            *samples_out = 0;
        }
        return false;
    }

    const uint32_t current_time = millis();

    int collected = 0;
    int32_t newest_raw = 0;
    int32_t oldest_raw = 0;
    uint32_t newest_ts = 0;
    uint32_t oldest_ts = 0;

    for (int i = 0; i < at.count; ++i) {
        uint16_t index = (at.write_index - 1 - i + MAX_BUFFER_SIZE) % MAX_BUFFER_SIZE;
        const AdcSample& sample = circular_buffer[index];
        const int32_t raw_value = sample.raw_value.load();
        const uint32_t timestamp_ms = sample.timestamp_ms.load();

        if (current_time - timestamp_ms > window_ms) {
            break;
        }

        if (collected == 0) {
            newest_raw = raw_value;
            newest_ts = timestamp_ms;
        }

        oldest_raw = raw_value;
        oldest_ts = timestamp_ms;
        ++collected;
    }

    if (samples_out) {
        *samples_out = collected;
    }

    if (collected < 2) {
        *delta_out = 0;
        if (span_ms_out) {
            *span_ms_out = 0;
        }
        return false;
    }

    *delta_out = newest_raw - oldest_raw;

    if (span_ms_out) {
        // Unsigned subtraction is also correct across a millis() wrap.
        *span_ms_out = newest_ts - oldest_ts;
    }

    return true;
}

bool CircularBufferMath::is_settled(uint32_t window_ms, int32_t threshold_raw_units) const {
    const uint32_t effective_window_ms = std::max(window_ms, MIN_SETTLING_WINDOW_MS);
    const int max_samples = calculate_max_samples_for_window(effective_window_ms);
    if (max_samples < MIN_SETTLING_SAMPLES) return false;

    int32_t* samples = (int32_t*)alloca(max_samples * sizeof(int32_t));
    const int actual_samples = get_samples_in_window(effective_window_ms, samples, max_samples);
    if (actual_samples < MIN_SETTLING_SAMPLES) return false;
            
    const float std_dev = calculate_standard_deviation(samples, actual_samples);
    // Samples are newest first; a steady rise or fall shows up end to end.
    const int64_t drift = static_cast<int64_t>(samples[0]) - samples[actual_samples - 1];
    const int64_t drift_limit = 2LL * threshold_raw_units;
    bool settled = std_dev <= threshold_raw_units && drift <= drift_limit && -drift <= drift_limit;
            
    // Debug output every 1s during settling checks (callers run on several tasks)
    static std::atomic<uint32_t> last_debug_time{0};
    if (millis() - last_debug_time.load() > 1000) {
        // Format raw samples on one line (limit to first 10 samples to avoid spam)
        char sample_str[256] = {0};
        int offset = 0;
        int samples_to_show = std::min(actual_samples, 10);
        for (int i = 0; i < samples_to_show; i++) {
            offset += snprintf(sample_str + offset, sizeof(sample_str) - offset,
                             "%ld%s", (long)samples[i], (i < samples_to_show - 1) ? "," : "");
        }
        if (actual_samples > 10) {
            offset += snprintf(sample_str + offset, sizeof(sample_str) - offset, "...");
        }

        LOG_LOADCELL_DEBUG("[SETTLING] Window:%lums Samples:%d Raw:[%s] StdDev:%.2f Drift:%lld Threshold:%ld Settled:%s\n",
                         effective_window_ms, actual_samples, sample_str, std_dev, (long long)drift,
                         (long)threshold_raw_units, settled ? "YES" : "NO");
        last_debug_time.store(millis());
    }
    
    return settled;
}

float CircularBufferMath::get_settling_confidence(uint32_t window_ms) const {
    // Calculate confidence based on standard deviation
    float std_dev = get_standard_deviation_raw(window_ms);
    
    // Confidence inversely related to standard deviation
    // This is a heuristic - may need tuning based on ADC characteristics
    float max_expected_std = 1000.0f; // Raw units
    float confidence = 1.0f - (std_dev / max_expected_std);
    
    return std::max(0.0f, std::min(1.0f, confidence));
}

float CircularBufferMath::get_standard_deviation_raw(uint32_t window_ms) const {
    // Calculate max samples needed
    int max_samples = calculate_max_samples_for_window(window_ms);
    if (max_samples == 0) return 0.0f;
    
    // Allocate temporary array
    int32_t* samples = (int32_t*)alloca(max_samples * sizeof(int32_t));
    
    // Get samples within time window
    int actual_samples = get_samples_in_window(window_ms, samples, max_samples);
    
    return calculate_standard_deviation(samples, actual_samples);
}

float CircularBufferMath::calculate_standard_deviation(const int32_t* samples, int count) const {
    if (count <= 1) return 0.0f;
    
    // Calculate mean
    int64_t sum = 0;
    for (int i = 0; i < count; i++) {
        sum += samples[i];
    }
    float mean = (float)sum / count;
    
    // Calculate variance
    float variance_sum = 0.0f;
    for (int i = 0; i < count; i++) {
        float diff = samples[i] - mean;
        variance_sum += diff * diff;
    }
    
    float variance = variance_sum / (count - 1);
    return sqrt(variance);
}

float CircularBufferMath::get_raw_flow_rate(uint32_t window_ms) const {
    // Calculate max samples needed
    int max_samples = calculate_max_samples_for_window(window_ms);
    if (max_samples < 2) return 0.0f;
    
    // Allocate temporary arrays
    int32_t* samples = (int32_t*)alloca(max_samples * sizeof(int32_t));
    uint32_t* ages = (uint32_t*)alloca(max_samples * sizeof(uint32_t));
    
    // Get samples and their ages within window
    const int collected = collect_window(window_ms, samples, ages, max_samples);
    
    if (collected < 2) return 0.0f;
    
    // Simple linear regression for flow rate
    int32_t raw_change = samples[0] - samples[collected - 1]; // Most recent - oldest
    uint32_t time_change = ages[collected - 1] - ages[0];
    
    if (time_change == 0) return 0.0f;
    
    // Return raw units per second
    return (float)raw_change * 1000.0f / time_change;
}

float CircularBufferMath::get_raw_flow_rate_95th_percentile(uint32_t window_ms) const {
    // Define parameters for the sub-window analysis
    const int MIN_SAMPLES_FOR_PERCENTILE = 10;
    const uint32_t SUB_WINDOW_MS = 300;
    const uint32_t STEP_MS = 100;
    const int MIN_SUB_WINDOWS = 4;
    const int MAX_SUB_WINDOWS = 32;
    const int MIN_SAMPLES_PER_SUB_WINDOW = 3;

    if (get_sample_count() < MIN_SAMPLES_FOR_PERCENTILE) {
        return get_raw_flow_rate(window_ms); // Fallback for insufficient data
    }

    // Ensure the window is large enough to contain a minimum number of samples
    uint32_t min_window_for_samples = (MIN_SAMPLES_FOR_PERCENTILE * 1000) / HW_LOADCELL_SAMPLE_RATE_SPS;
    uint32_t effective_window_ms = std::max(window_ms, min_window_for_samples);

    // 1. Collect all relevant samples and their ages in one go.
    int max_samples = calculate_max_samples_for_window(effective_window_ms);
    if (max_samples < MIN_SAMPLES_FOR_PERCENTILE) {
        return get_raw_flow_rate(effective_window_ms);
    }

    int32_t* sample_values = (int32_t*)alloca(max_samples * sizeof(int32_t));
    uint32_t* sample_ages = (uint32_t*)alloca(max_samples * sizeof(uint32_t));
    // Samples are collected from newest to oldest
    const int collected_samples = collect_window(effective_window_ms, sample_values, sample_ages, max_samples);

    if (collected_samples < MIN_SAMPLES_FOR_PERCENTILE) {
        return get_raw_flow_rate(effective_window_ms);
    }

    // 2. Calculate the number of sub-windows and allocate space for their flow rates.
    int num_sub_windows = (effective_window_ms > SUB_WINDOW_MS) ? 1 + (effective_window_ms - SUB_WINDOW_MS) / STEP_MS : 1;
    num_sub_windows = std::max(MIN_SUB_WINDOWS, std::min(MAX_SUB_WINDOWS, num_sub_windows));
    float* flow_rates = (float*)alloca(num_sub_windows * sizeof(float));
    int valid_flow_rates_count = 0;

    // 3. Iterate through sub-windows and calculate flow rate for each.
    for (int i = 0; i < num_sub_windows; ++i) {
        // Sub-window i covers sample ages [i * STEP_MS, i * STEP_MS + SUB_WINDOW_MS].
        uint32_t sub_window_newest_age = i * STEP_MS;
        uint32_t sub_window_oldest_age = sub_window_newest_age + SUB_WINDOW_MS;

        // Find the newest and oldest samples within this sub-window from our collected arrays
        int newest_idx = -1, oldest_idx = -1;
        for (int j = 0; j < collected_samples; ++j) {
            if (sample_ages[j] >= sub_window_newest_age) {
                if (newest_idx == -1) newest_idx = j;
                if (sample_ages[j] <= sub_window_oldest_age) {
                    oldest_idx = j;
                } else {
                    break; // Past the start of the sub-window
                }
            }
        }

        if (newest_idx != -1 && oldest_idx != -1 && (oldest_idx - newest_idx + 1) >= MIN_SAMPLES_PER_SUB_WINDOW) {
            uint32_t time_delta = sample_ages[oldest_idx] - sample_ages[newest_idx];
            if (time_delta > 0) {
                int32_t raw_delta = sample_values[newest_idx] - sample_values[oldest_idx];
                flow_rates[valid_flow_rates_count++] = (float)raw_delta * 1000.0f / time_delta;
            }
        }
    }

    // 4. Calculate the 95th percentile from the collected flow rates.
    if (valid_flow_rates_count >= MIN_SAMPLES_PER_SUB_WINDOW) {
        std::sort(flow_rates, flow_rates + valid_flow_rates_count);
        int percentile_95_index = static_cast<int>(valid_flow_rates_count * 0.95f);
        percentile_95_index = std::min(percentile_95_index, valid_flow_rates_count - 1);
        return flow_rates[percentile_95_index];
    }

    // Fallback if we couldn't get enough valid sub-window rates
    return get_raw_flow_rate(effective_window_ms);
}

bool CircularBufferMath::raw_flowrate_is_stable(uint32_t window_ms) const {
    // Simple stability check - compare recent flow rates
    float current_flow = get_raw_flow_rate(window_ms);
    float recent_flow = get_raw_flow_rate(window_ms / 2); // Half window
    
    // Consider stable if flow rates are within 10% of each other
    float threshold = abs(current_flow) * 0.1f;
    return abs(current_flow - recent_flow) <= threshold;
}

int32_t CircularBufferMath::get_min_raw(uint32_t window_ms) const {
    const Snapshot at = snapshot();
    if (at.count == 0) return 0;

    const uint32_t current_time = millis();
    bool found = false;
    int32_t min_val = 0;
    for (int i = 0; i < at.count; ++i) {
        const uint16_t index = (at.write_index - 1 - i + MAX_BUFFER_SIZE) % MAX_BUFFER_SIZE;
        const AdcSample& sample = circular_buffer[index];
        if (current_time - sample.timestamp_ms.load() > window_ms) break;
        const int32_t raw_value = sample.raw_value.load();
        if (!found || raw_value < min_val) min_val = raw_value;
        found = true;
    }
    return found ? min_val : 0;
}

int32_t CircularBufferMath::get_max_raw(uint32_t window_ms) const {
    const Snapshot at = snapshot();
    if (at.count == 0) return 0;

    const uint32_t current_time = millis();
    bool found = false;
    int32_t max_val = 0;
    for (int i = 0; i < at.count; ++i) {
        const uint16_t index = (at.write_index - 1 - i + MAX_BUFFER_SIZE) % MAX_BUFFER_SIZE;
        const AdcSample& sample = circular_buffer[index];
        if (current_time - sample.timestamp_ms.load() > window_ms) break;
        const int32_t raw_value = sample.raw_value.load();
        if (!found || raw_value > max_val) max_val = raw_value;
        found = true;
    }
    return found ? max_val : 0;
}

void CircularBufferMath::reset_display_filter() {
    display_filter_initialized_.store(false);
    display_filtered_raw_.store(0);
}

void CircularBufferMath::clear_all_samples() {
    display_filter_initialized_.store(false);
    // Applied by the sampling task before its next sample; until then every
    // reader sees an empty buffer. Slots need no zeroing: only the published
    // count makes a slot readable.
    clear_requests_.fetch_add(1, std::memory_order_acq_rel);
}
