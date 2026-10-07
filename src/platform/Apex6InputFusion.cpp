#include "platform/Apex6InputFusion.h"

#include "platform/XInputMapping.h"

#include <algorithm>

namespace asb::platform {

Apex6InputFusion::Apex6InputFusion(
    std::chrono::milliseconds heartbeat) noexcept
    : heartbeat_((std::max)(heartbeat, std::chrono::milliseconds(1))) {}

std::optional<dualsense::DualSenseInputState> Apex6InputFusion::observe(
    const std::optional<dualsense::DualSenseInputState>& mappedState,
    const dualsense::DualSenseInputState& xinputState,
    Clock::time_point now) noexcept {
    if (mappedState) lastMappedState_ = *mappedState;

    // XInput is a safe bootstrap until Windows publishes the first mapped-HID
    // report. Once HID has spoken, only its LT/RT fields are ever replaced.
    auto fused = lastMappedState_.value_or(xinputState);
    mergeIndependentTriggers(xinputState.l2, xinputState.r2, fused);

    const bool firstState = !lastPublishedState_;
    const bool changed = !firstState && fused != *lastPublishedState_;
    const bool heartbeatDue = !firstState && now - lastPublishedAt_ >= heartbeat_;
    if (!mappedState && !firstState && !changed && !heartbeatDue) {
        return std::nullopt;
    }

    lastPublishedState_ = fused;
    lastPublishedAt_ = now;
    return fused;
}

} // namespace asb::platform
