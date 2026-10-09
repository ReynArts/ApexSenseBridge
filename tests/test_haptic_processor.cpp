#ifdef NDEBUG
#undef NDEBUG
#endif

#include "haptics/HapticProcessor.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

int main() {
    using namespace asb;

    haptics::HapticProcessor processor;
    dualsense::DualSenseFeedback feedback{};
    feedback.kind = dualsense::FeedbackKind::AudioHaptics;

    assert(processor.process(feedback) == haptics::MotorLevels{});

    feedback.leftEnergy = 500;
    feedback.leftPeak = 700;
    feedback.leftTransient = 250;
    assert(processor.process(feedback) == haptics::MotorLevels{});

    feedback.leftEnergy = 65535;
    feedback.leftPeak = 65535;
    feedback.leftTransient = 65535;
    const auto fullLeft = processor.process(feedback);
    assert(fullLeft.lowFrequency == 217);
    assert(fullLeft.highFrequency == 0);

    feedback = {};
    feedback.kind = dualsense::FeedbackKind::AudioHaptics;
    feedback.rightEnergy = 65535;
    feedback.rightPeak = 65535;
    feedback.rightTransient = 65535;
    const auto fullRight = processor.process(feedback);
    assert(fullRight.lowFrequency == 0);
    assert(fullRight.highFrequency == 217);

    feedback = {};
    feedback.kind = dualsense::FeedbackKind::AudioHaptics;
    feedback.leftPeak = 5000;
    assert(processor.process(feedback).lowFrequency == 0);
    feedback.leftPeak = 20000;
    const auto subtle = processor.process(feedback).lowFrequency;
    feedback.leftPeak = 50000;
    const auto strong = processor.process(feedback).lowFrequency;
    assert(subtle > 0);
    assert(strong > subtle);

    haptics::HapticConfig openGate{};
    openGate.activationThreshold = 0.0;
    haptics::HapticProcessor openProcessor(openGate);
    feedback.leftPeak = 5000;
    assert(openProcessor.process(feedback).lowFrequency > 0);

    // Threshold 0 keeps the original linear mapping exactly.
    for (unsigned peak = 0; peak <= 65535; peak += 97) {
        feedback = {};
        feedback.kind = dualsense::FeedbackKind::AudioHaptics;
        feedback.leftPeak = static_cast<std::uint16_t>(peak);
        const double unit = peak <= 700 ? 0.0 : (peak - 700.0) / (65535.0 - 700.0);
        const double combined = std::clamp(unit * 0.50, 0.0, 1.0);
        const auto expected = static_cast<std::uint8_t>(
            std::lround(std::pow(combined, 0.72) * 0.85 * 255.0));
        assert(openProcessor.process(feedback).lowFrequency == expected);
    }

    // The soft knee fades in around the threshold: monotonic, no jumps, and
    // never quieter than the former hard gate.
    haptics::HapticConfig kneeConfig{};
    kneeConfig.activationThreshold = 0.5;
    haptics::HapticProcessor kneeProcessor(kneeConfig);
    int previous = 0;
    bool fadedBelowThreshold = false;
    for (unsigned peak = 0; peak <= 65535; peak += 16) {
        feedback = {};
        feedback.kind = dualsense::FeedbackKind::AudioHaptics;
        feedback.leftPeak = static_cast<std::uint16_t>(peak);
        feedback.leftEnergy = static_cast<std::uint16_t>(peak);
        feedback.leftTransient = static_cast<std::uint16_t>(peak);
        const int level = kneeProcessor.process(feedback).lowFrequency;
        assert(level >= previous);
        assert(level - previous <= 8);
        const double unit = (peak - 0.0) / 65535.0;
        if (unit < 0.5 - 0.08) assert(level == 0);
        if (unit > 0.45 && unit < 0.5 && level > 0) fadedBelowThreshold = true;
        previous = level;
    }
    assert(fadedBelowThreshold);
    assert(previous == 217);

    feedback.kind = dualsense::FeedbackKind::HidOutput;
    assert(processor.process(feedback) == haptics::MotorLevels{});
    return 0;
}
