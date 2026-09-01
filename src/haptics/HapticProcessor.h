#pragma once

#include "dualsense/DualSenseFeedback.h"

#include <cstdint>

namespace asb::haptics {

struct MotorLevels {
    std::uint8_t lowFrequency = 0;
    std::uint8_t highFrequency = 0;

    bool operator==(const MotorLevels&) const = default;
};

struct HapticConfig {
    std::uint16_t energyFloor = 500;
    std::uint16_t peakFloor = 700;
    std::uint16_t transientFloor = 250;
    double energyWeight = 0.16;
    double peakWeight = 0.50;
    double transientWeight = 0.34;
    double activationThreshold = 0.12;
    double responseCurve = 0.72;
    double outputGain = 0.85;
};

// VIIPER has already reduced each 240-sample (5 ms at 48 kHz) audio window to
// energy, peak, and rising-transient measurements. This processor performs the
// remaining perceptual mapping without allocations or additional buffering.
class HapticProcessor {
public:
    explicit HapticProcessor(HapticConfig config = {});

    [[nodiscard]] MotorLevels process(
        const dualsense::DualSenseFeedback& feedback) const noexcept;

private:
    HapticConfig config_;
};

} // namespace asb::haptics
