#pragma once

#include "core/TriggerEffect.h"

#include <array>
#include <cstdint>
#include <optional>

namespace asb::dualsense {

// Translate a DualSense trigger's [type + 10 parameters] block using the
// mappings measured by OpenFlydigi. Unknown effects deliberately mean
// "leave the current effect unchanged", not "clear".
std::optional<ForceTriggerCommand> translateAdaptiveTrigger(
    TriggerSide side,
    const std::array<std::uint8_t, 11>& effect,
    std::uint8_t leftMotor);

} // namespace asb::dualsense
