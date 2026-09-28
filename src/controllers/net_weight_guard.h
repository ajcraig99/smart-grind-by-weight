#pragma once

#include <cstdint>

class NetWeightRemovalGuard {
public:
    static constexpr float MINIMUM_REFERENCE_WEIGHT_G = 5.0f;
    static constexpr float REMOVAL_THRESHOLD_RATIO = 0.90f;
    // The zero usually already includes the vessel (boot tare, or the tare of
    // the previous grind), so there is often no reference. Lifting any vessel
    // heavier than this still reads below it.
    static constexpr float NO_REFERENCE_REMOVAL_THRESHOLD_G = -10.0f;
    static constexpr uint8_t REQUIRED_CONSECUTIVE_SAMPLES = 3;

    void reset(float pre_tare_weight_g) {
        reference_weight_g_ = pre_tare_weight_g >= MINIMUM_REFERENCE_WEIGHT_G
                                  ? pre_tare_weight_g
                                  : 0.0f;
        consecutive_samples_ = 0;
    }

    // Call once per ADC sample, not per control cycle.
    bool update(float net_weight_g) {
        if (net_weight_g > removal_threshold_g()) {
            consecutive_samples_ = 0;
            return false;
        }

        if (consecutive_samples_ < REQUIRED_CONSECUTIVE_SAMPLES) {
            ++consecutive_samples_;
        }
        return consecutive_samples_ >= REQUIRED_CONSECUTIVE_SAMPLES;
    }

    bool has_reference() const { return reference_weight_g_ > 0.0f; }
    void cancel_pending() { consecutive_samples_ = 0; }
    float reference_weight_g() const { return reference_weight_g_; }
    float removal_threshold_g() const {
        return has_reference() ? -reference_weight_g_ * REMOVAL_THRESHOLD_RATIO
                               : NO_REFERENCE_REMOVAL_THRESHOLD_G;
    }

private:
    float reference_weight_g_ = 0.0f;
    uint8_t consecutive_samples_ = 0;
};
