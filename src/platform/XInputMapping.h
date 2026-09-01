#pragma once

#include "dualsense/DualSenseInput.h"

#include <cstdint>

namespace asb::platform {

struct XInputSnapshot {
    std::int16_t leftX = 0;
    std::int16_t leftY = 0;
    std::int16_t rightX = 0;
    std::int16_t rightY = 0;
    std::uint8_t leftTrigger = 0;
    std::uint8_t rightTrigger = 0;
    std::uint16_t buttons = 0;
};

namespace xinputButton {
inline constexpr std::uint16_t kDpadUp = 0x0001;
inline constexpr std::uint16_t kDpadDown = 0x0002;
inline constexpr std::uint16_t kDpadLeft = 0x0004;
inline constexpr std::uint16_t kDpadRight = 0x0008;
inline constexpr std::uint16_t kStart = 0x0010;
inline constexpr std::uint16_t kBack = 0x0020;
inline constexpr std::uint16_t kLeftThumb = 0x0040;
inline constexpr std::uint16_t kRightThumb = 0x0080;
inline constexpr std::uint16_t kLeftShoulder = 0x0100;
inline constexpr std::uint16_t kRightShoulder = 0x0200;
inline constexpr std::uint16_t kA = 0x1000;
inline constexpr std::uint16_t kB = 0x2000;
inline constexpr std::uint16_t kX = 0x4000;
inline constexpr std::uint16_t kY = 0x8000;
inline constexpr std::uint8_t kTriggerThreshold = 30;
} // namespace xinputButton

// Applies only XInput digital controls and trigger-button thresholds. Axes and
// analog trigger values are copied by the platform poller.
void mapXInputButtons(std::uint16_t xinputButtons,
                      std::uint8_t leftTrigger,
                      std::uint8_t rightTrigger,
                      dualsense::DualSenseInputState& state) noexcept;

// Complete, allocation-free translation used by the fallback proxy and by
// capture-replay tests. Y axes are inverted to DualSense USB coordinates.
dualsense::DualSenseInputState mapXInputState(
    const XInputSnapshot& snapshot) noexcept;

} // namespace asb::platform
