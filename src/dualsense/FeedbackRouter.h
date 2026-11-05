#pragma once

#include "dualsense/DualSenseFeedback.h"

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace asb::dualsense {

class FeedbackRouter {
public:
    using Sink = std::function<void(const DualSenseFeedback&)>;

    void route(const DualSenseFeedback& feedback) {
        std::lock_guard lock(mutex_);
        remember(feedback);
        if (sink_) sink_(feedback);
    }

    void attach(Sink sink) {
        std::lock_guard lock(mutex_);
        sink_ = std::move(sink);
        if (!sink_) return;
        replayLocked();
    }

    void detach() {
        std::lock_guard lock(mutex_);
        sink_ = nullptr;
    }

    [[nodiscard]] bool attached() const {
        std::lock_guard lock(mutex_);
        return static_cast<bool>(sink_);
    }

private:
    static constexpr std::uint8_t kRightTriggerBit = 0x04;
    static constexpr std::uint8_t kLeftTriggerBit = 0x08;

    void remember(const DualSenseFeedback& feedback) {
        if (feedback.kind != FeedbackKind::HidOutput) return;
        if ((feedback.enableBits1 & kRightTriggerBit) != 0 && feedback.rightTriggerEffect[0] != 0) {
            rightTrigger_ = feedback.rightTriggerEffect;
        }
        if ((feedback.enableBits1 & kLeftTriggerBit) != 0 && feedback.leftTriggerEffect[0] != 0) {
            leftTrigger_ = feedback.leftTriggerEffect;
        }
        if (feedback.hasLightbarColor()) {
            lightbar_ = std::array<std::uint8_t, 3>{
                feedback.lightbarRed, feedback.lightbarGreen, feedback.lightbarBlue};
        }
    }

    void replayLocked() {
        if (rightTrigger_ || leftTrigger_) {
            DualSenseFeedback frame{};
            frame.kind = FeedbackKind::HidOutput;
            if (rightTrigger_) {
                frame.enableBits1 = static_cast<std::uint8_t>(frame.enableBits1 | kRightTriggerBit);
                frame.rightTriggerEffect = *rightTrigger_;
            }
            if (leftTrigger_) {
                frame.enableBits1 = static_cast<std::uint8_t>(frame.enableBits1 | kLeftTriggerBit);
                frame.leftTriggerEffect = *leftTrigger_;
            }
            sink_(frame);
        }
        if (lightbar_) {
            DualSenseFeedback frame{};
            frame.kind = FeedbackKind::HidOutput;
            frame.hasLightbar = true;
            frame.lightbarRed = (*lightbar_)[0];
            frame.lightbarGreen = (*lightbar_)[1];
            frame.lightbarBlue = (*lightbar_)[2];
            sink_(frame);
        }
    }

    mutable std::mutex mutex_;
    Sink sink_;
    std::optional<std::array<std::uint8_t, 11>> rightTrigger_;
    std::optional<std::array<std::uint8_t, 11>> leftTrigger_;
    std::optional<std::array<std::uint8_t, 3>> lightbar_;
};

}
