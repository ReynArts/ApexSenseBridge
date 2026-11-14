#include "platform/Apex6InputFusion.h"

#include "platform/XInputMapping.h"

#include <algorithm>

namespace asb::platform {

Apex6InputFusion::Apex6InputFusion(
    std::chrono::milliseconds heartbeat) noexcept
    : heartbeat_((std::max)(heartbeat, std::chrono::milliseconds(1))) {}

void Apex6InputFusion::setXInputAvailable(bool available) noexcept {
    xinputAvailable_ = available;
    if (!available) {
        fallbackActive_ = true;
        stats_.fallbackUsed = true;
    }
}

void Apex6InputFusion::setFallback(bool active) noexcept {
    if (active == fallbackActive_) return;
    fallbackActive_ = active;
    stats_.fallbackUsed = stats_.fallbackUsed || active;
    ++stats_.fallbackSwitches;
    independent_ = false;
}

void Apex6InputFusion::trackXInputHealth(
    const dualsense::DualSenseInputState& xinputState,
    const std::optional<Apex6VendorTriggers>& vendorTriggers,
    Clock::time_point now) noexcept {
    if (!xinputAvailable_) return;

    stats_.xinputMaxL2 = (std::max)(stats_.xinputMaxL2, xinputState.l2);
    stats_.xinputMaxR2 = (std::max)(stats_.xinputMaxR2, xinputState.r2);

    if (xinputState.l2 > 0 || xinputState.r2 > 0) {
        releasedSince_.reset();
        setFallback(false);
        return;
    }

    std::uint8_t reference = 0;
    if (lastMappedState_) {
        reference = (std::max)(lastMappedState_->l2, lastMappedState_->r2);
    }
    if (vendorTriggers && now - vendorTriggers->at <= kVendorFreshness) {
        reference = (std::max)(reference,
                               (std::max)(vendorTriggers->left, vendorTriggers->right));
    }
    if (reference < kActivityFloor) {
        releasedSince_.reset();
        return;
    }
    if (!releasedSince_) {
        releasedSince_ = now;
    } else if (now - *releasedSince_ >= kDeadConfirmation) {
        setFallback(true);
    }
}

void Apex6InputFusion::applyFallbackTriggers(
    const std::optional<Apex6VendorTriggers>& vendorTriggers,
    Clock::time_point now,
    dualsense::DualSenseInputState& state) noexcept {
    bool independent = false;
    if (vendorTriggers && now - vendorTriggers->at <= kVendorFreshness) {
        independent = independent_
            ? vendorTriggers->left >= kIndependentReleaseFloor &&
                  vendorTriggers->right >= kIndependentReleaseFloor
            : vendorTriggers->left > kIndependentEngageFloor &&
                  vendorTriggers->right > kIndependentEngageFloor;
    }
    independent_ = independent;
    if (independent) {
        mergeIndependentTriggers(vendorTriggers->left, vendorTriggers->right, state);
    }
}

std::optional<dualsense::DualSenseInputState> Apex6InputFusion::observe(
    const std::optional<dualsense::DualSenseInputState>& mappedState,
    const dualsense::DualSenseInputState& xinputState,
    Clock::time_point now) noexcept {
    return observe(mappedState, xinputState, std::nullopt, std::nullopt, now);
}

std::optional<dualsense::DualSenseInputState> Apex6InputFusion::observe(
    const std::optional<dualsense::DualSenseInputState>& mappedState,
    const dualsense::DualSenseInputState& xinputState,
    const std::optional<flydigi::apex6::MotionSample>& motion,
    Clock::time_point now) noexcept {
    return observe(mappedState, xinputState, motion, std::nullopt, now);
}

std::optional<dualsense::DualSenseInputState> Apex6InputFusion::observe(
    const std::optional<dualsense::DualSenseInputState>& mappedState,
    const dualsense::DualSenseInputState& xinputState,
    const std::optional<flydigi::apex6::MotionSample>& motion,
    const std::optional<Apex6VendorTriggers>& vendorTriggers,
    Clock::time_point now) noexcept {
    if (mappedState) {
        lastMappedState_ = *mappedState;
        stats_.mappedMaxL2 = (std::max)(stats_.mappedMaxL2, mappedState->l2);
        stats_.mappedMaxR2 = (std::max)(stats_.mappedMaxR2, mappedState->r2);
    }
    if (vendorTriggers) {
        stats_.vendorMaxL2 = (std::max)(stats_.vendorMaxL2, vendorTriggers->left);
        stats_.vendorMaxR2 = (std::max)(stats_.vendorMaxR2, vendorTriggers->right);
    }
    if (!xinputAvailable_ && !lastMappedState_) return std::nullopt;

    trackXInputHealth(xinputState, vendorTriggers, now);

    auto fused = lastMappedState_.value_or(xinputState);
    if (fallbackActive_ && lastMappedState_) {
        applyFallbackTriggers(vendorTriggers, now, fused);
    } else {
        mergeIndependentTriggers(xinputState.l2, xinputState.r2, fused);
    }
    if (motion) {
        fused.gyroX = motion->gyroX;
        fused.gyroY = motion->gyroY;
        fused.gyroZ = motion->gyroZ;
        fused.accelX = motion->accelX;
        fused.accelY = motion->accelY;
        fused.accelZ = motion->accelZ;
    }

    const bool firstState = !lastPublishedState_;
    const bool changed = !firstState && fused != *lastPublishedState_;
    const bool heartbeatDue = !firstState && now - lastPublishedAt_ >= heartbeat_;
    if (!mappedState && !firstState && !changed && !heartbeatDue) {
        return std::nullopt;
    }

    lastPublishedState_ = fused;
    lastPublishedAt_ = now;
    if (fallbackActive_) ++stats_.fallbackReports;
    return fused;
}

} // namespace asb::platform
