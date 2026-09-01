#include "haptics/HapticProcessor.h"

#include <algorithm>
#include <cmath>

namespace asb::haptics {
namespace {

double toUnit(std::uint16_t value, std::uint16_t floor) noexcept {
    if (value <= floor || floor == 0xFFFFu) {
        return 0.0;
    }
    return std::clamp(static_cast<double>(value - floor) /
                          static_cast<double>(0xFFFFu - floor),
                      0.0, 1.0);
}

std::uint8_t mapChannel(std::uint16_t energy,
                        std::uint16_t peak,
                        std::uint16_t transient,
                        const HapticConfig& config) noexcept {
    const double energyUnit = toUnit(energy, config.energyFloor);
    const double peakUnit = toUnit(peak, config.peakFloor);
    const double transientUnit = toUnit(transient, config.transientFloor);
    const double combined = std::clamp(
        energyUnit * config.energyWeight +
            peakUnit * config.peakWeight +
            transientUnit * config.transientWeight,
        0.0, 1.0);
    const double threshold = std::clamp(config.activationThreshold, 0.0, 0.95);
    if (combined <= threshold) {
        return 0;
    }

    // Remove the low-level texture that makes conventional eccentric motors
    // feel permanently active, then expand the useful range back to 0..1 so
    // the game's remaining intensity variations are not compressed.
    const double gated = (combined - threshold) / (1.0 - threshold);
    const double curve = std::clamp(config.responseCurve, 0.01, 4.0);
    const double output = std::pow(gated, curve) *
                          std::clamp(config.outputGain, 0.0, 1.0) * 255.0;
    return static_cast<std::uint8_t>(
        std::clamp<long>(std::lround(output), 0, 255));
}

} // namespace

HapticProcessor::HapticProcessor(HapticConfig config)
    : config_(config) {}

MotorLevels HapticProcessor::process(
    const dualsense::DualSenseFeedback& feedback) const noexcept {
    if (feedback.kind != dualsense::FeedbackKind::AudioHaptics) {
        return {};
    }

    // VIIPER's left/right fields are the two DualSense haptic audio channels.
    // Command 0x12 exposes the APEX left/low and right/high grip motors in the
    // corresponding byte order, preserving the game's spatial separation.
    return {
        mapChannel(feedback.leftEnergy, feedback.leftPeak,
                   feedback.leftTransient, config_),
        mapChannel(feedback.rightEnergy, feedback.rightPeak,
                   feedback.rightTransient, config_),
    };
}

} // namespace asb::haptics
