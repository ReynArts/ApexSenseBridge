#pragma once

#include "dualsense/DualSenseInput.h"

#include <cstdint>
#include <optional>
#include <span>

namespace asb::flydigi {

// Decodes Flydigi V1 state report 04 FE. This is the full 32-byte state sent
// on the Apex 4 vendor interface, independent from its 10-byte DInput view.
[[nodiscard]] std::optional<dualsense::DualSenseInputState>
decodeApex4InputReport(std::span<const std::uint8_t> report) noexcept;

} // namespace asb::flydigi
