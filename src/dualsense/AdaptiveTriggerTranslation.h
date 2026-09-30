#pragma once

#include "core/TriggerEffect.h"

#include <array>
#include <cstdint>
#include <optional>

namespace asb::dualsense {

std::optional<ForceTriggerCommand> translateAdaptiveTrigger(
    TriggerSide side,
    const std::array<std::uint8_t, 11>& effect,
    std::uint8_t);

} // namespace asb::dualsense
