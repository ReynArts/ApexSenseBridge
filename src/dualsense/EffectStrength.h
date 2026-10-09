#pragma once

#include "core/TriggerEffect.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace asb::dualsense {

template<class T>
[[nodiscard]] T scaleEffectStrength(T value, unsigned percent) noexcept {
    return static_cast<T>(static_cast<int>(value) * static_cast<int>((std::min)(percent, 100U)) / 100);
}

// Conventional grip-motor gain only. Never use this on signed PCM samples or
// trigger force fields. At or below 100% it is linear; above, a saturating
// curve lifts low/mid/high levels without hard clipping or wrapping.
[[nodiscard]] inline std::uint8_t scaleRumbleStrength(std::uint8_t value,
                                                     unsigned percent) noexcept {
    const unsigned capped = (std::min)(percent, 200U);
    if (capped <= 100U) {
        return static_cast<std::uint8_t>(static_cast<unsigned>(value) * capped / 100U);
    }
    const double remaining = 1.0 - static_cast<double>(value) / 255.0;
    const double exponent = 2.0 * static_cast<double>(capped) / 100.0 - 1.0;
    const long lifted = std::lround(255.0 * (1.0 - std::pow(remaining, exponent)));
    return static_cast<std::uint8_t>(std::clamp<long>(lifted, value, 255));
}

// Scale only force fields. Travel, zones, frequency and effect timing stay intact.
[[nodiscard]] inline ForceTriggerCommand scaleTriggerStrength(ForceTriggerCommand command, unsigned percent) noexcept {
    if (percent >= 100 || command.mode == TriggerMode::Normal) return command;
    if (percent == 0) return {command.side, TriggerMode::Normal, {}};
    auto scale = [percent](std::uint8_t value) { return scaleEffectStrength(value, percent); };
    switch (command.mode) {
    case TriggerMode::Race: command.params[1] = scale(command.params[1]); break;
    case TriggerMode::SniperBreak: command.params[2] = scale(command.params[2]); break;
    case TriggerMode::RecoilRattle: command.params[2] = scale(command.params[2]); break;
    default: break;
    }
    if ((command.mode == TriggerMode::Race && command.params[1] == 0) ||
        ((command.mode == TriggerMode::SniperBreak || command.mode == TriggerMode::RecoilRattle) && command.params[2] == 0))
        return {command.side, TriggerMode::Normal, {}};
    return command;
}

} // namespace asb::dualsense
