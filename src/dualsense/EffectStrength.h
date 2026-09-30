#pragma once

#include "core/TriggerEffect.h"
#include <algorithm>
#include <cstdint>

namespace asb::dualsense {

template<class T>
[[nodiscard]] T scaleEffectStrength(T value, unsigned percent) noexcept {
    return static_cast<T>(static_cast<int>(value) * static_cast<int>((std::min)(percent, 100U)) / 100);
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
