#include "platform/SessionTuning.h"

namespace asb::platform {
namespace {

std::optional<std::uint32_t> boundedValue(std::uint32_t value, std::uint32_t maximum) noexcept {
    if (value == kSessionTuningKeep || value > maximum) return std::nullopt;
    return value;
}

std::optional<bool> flagValue(std::uint32_t value) noexcept {
    if (value > 1U) return std::nullopt;
    return value == 1U;
}

} // namespace

std::string sessionTuningMappingName(std::string_view token) {
    std::string result = "Local\\ApexSenseBridge.Session.";
    result.append(token);
    result.append(".Tuning");
    return result;
}

SessionTuningChange interpretSessionTuningRequest(
    const SessionTuningRequest& request,
    const SessionTuningLimits& limits) noexcept {
    SessionTuningChange change;
    change.triggerStrengthPercent = boundedValue(request.triggerStrengthPercent, 100U);
    change.vibrationStrengthPercent =
        boundedValue(request.vibrationStrengthPercent, limits.maxVibrationPercent);
    change.gripMute = flagValue(request.gripMute);
    change.triggerEffectsOff = flagValue(request.triggerEffectsOff);
    change.touchpadProfile = boundedValue(request.touchpadProfile, limits.maxTouchpadProfile);
    change.apexProfileSlot = boundedValue(request.apexProfileSlot, limits.maxApexSlot);
    return change;
}

} // namespace asb::platform
