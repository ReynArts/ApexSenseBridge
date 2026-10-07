#include "dualsense/AdaptiveTriggerBridge.h"

#include "dualsense/AdaptiveTriggerTranslation.h"
#include "dualsense/EffectStrength.h"

namespace asb::dualsense {

AdaptiveTriggerBridge::AdaptiveTriggerBridge(flydigi::Apex5Device& device, unsigned strengthPercent)
    : device_(device), strengthPercent_(strengthPercent) {}

void AdaptiveTriggerBridge::handle(const DualSenseFeedback& feedback) {
    if (failed_.load(std::memory_order_relaxed) || feedback.kind != FeedbackKind::HidOutput) return;

    constexpr std::uint8_t kRightTrigger = 0x04;
    constexpr std::uint8_t kLeftTrigger = 0x08;
    if ((feedback.enableBits1 & kRightTrigger) != 0) {
        apply(TriggerSide::Right, feedback.rightTriggerEffect);
    }
    if ((feedback.enableBits1 & kLeftTrigger) != 0) {
        apply(TriggerSide::Left, feedback.leftTriggerEffect);
    }
}

void AdaptiveTriggerBridge::apply(TriggerSide side,
                                  const std::array<std::uint8_t, 11>& effect) {
    if (effect[0] == 0) {
        neutral_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    (side == TriggerSide::Left ? lastLeftType_ : lastRightType_)
        .store(effect[0], std::memory_order_relaxed);
    auto translated = translateAdaptiveTrigger(side, effect, 0);
    if (!translated) {
        unsupported_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    *translated = scaleTriggerStrength(*translated, strengthPercent_);
    {
        std::lock_guard lock(stateMutex_);
        auto& previous = side == TriggerSide::Left ? lastLeft_ : lastRight_;
        if (previous == translated) {
            deduplicated_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    std::string writeError;
    if (!device_.queueTriggerRaw(*translated, writeError)) {
        writeFailures_.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard lock(errorMutex_);
            error_ = std::move(writeError);
        }
        failed_.store(true, std::memory_order_relaxed);
        return;
    }
    {
        std::lock_guard lock(stateMutex_);
        (side == TriggerSide::Left ? lastLeft_ : lastRight_) = translated;
        if (translated->mode != TriggerMode::Normal) {
            (side == TriggerSide::Left ? lastActiveLeftType_ : lastActiveRightType_) =
                effect[0];
            (side == TriggerSide::Left ? lastActiveLeft_ : lastActiveRight_) =
                translated;
        }
    }
    translated_.fetch_add(1, std::memory_order_relaxed);
    if (translated->mode == TriggerMode::Normal) {
        normal_.fetch_add(1, std::memory_order_relaxed);
    } else {
        active_.fetch_add(1, std::memory_order_relaxed);
    }
}

bool AdaptiveTriggerBridge::failed() const noexcept {
    return failed_.load(std::memory_order_relaxed);
}

std::string AdaptiveTriggerBridge::error() const {
    std::lock_guard lock(errorMutex_);
    return error_;
}

AdaptiveTriggerBridgeStats AdaptiveTriggerBridge::stats() const noexcept {
    std::lock_guard lock(stateMutex_);
    return {translated_.load(std::memory_order_relaxed),
            active_.load(std::memory_order_relaxed),
            normal_.load(std::memory_order_relaxed),
            deduplicated_.load(std::memory_order_relaxed),
            neutral_.load(std::memory_order_relaxed),
            unsupported_.load(std::memory_order_relaxed),
            writeFailures_.load(std::memory_order_relaxed),
            lastLeftType_.load(std::memory_order_relaxed),
            lastRightType_.load(std::memory_order_relaxed),
            lastLeft_, lastRight_,
            lastActiveLeftType_, lastActiveRightType_,
            lastActiveLeft_, lastActiveRight_};
}

} // namespace asb::dualsense
