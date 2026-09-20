#pragma once

#include "dualsense/DualSenseInput.h"

#include <cstdint>
#include <optional>
#include <span>

namespace asb::flydigi {

// Decodes the Apex 5 NewXInput operator report (command 0xEF) carried by the
// vendor HID interface. This stream remains available while the controller's
// ordinary XInput output is disabled, allowing a single visible DualSense to
// carry every standard control plus its gyroscope and accelerometer.
[[nodiscard]] std::optional<dualsense::DualSenseInputState>
decodeApex5InputReport(std::span<const std::uint8_t> report,
                       std::uint8_t batteryPercent = 100,
                       std::uint8_t chargeState = 0) noexcept;

} // namespace asb::flydigi
