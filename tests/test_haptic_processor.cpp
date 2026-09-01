#ifdef NDEBUG
#undef NDEBUG
#endif

#include "haptics/HapticProcessor.h"

#include <cassert>

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

    feedback.kind = dualsense::FeedbackKind::HidOutput;
    assert(processor.process(feedback) == haptics::MotorLevels{});
    return 0;
}
