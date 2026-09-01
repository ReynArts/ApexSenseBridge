#pragma once

#include <array>
#include <cstdint>

namespace asb::dualsense {

namespace button {
inline constexpr std::uint16_t kPs = 0x0001;
inline constexpr std::uint16_t kTouchpadClick = 0x0002;
inline constexpr std::uint16_t kMute = 0x0004;
inline constexpr std::uint16_t kSquare = 0x0010;
inline constexpr std::uint16_t kCross = 0x0020;
inline constexpr std::uint16_t kCircle = 0x0040;
inline constexpr std::uint16_t kTriangle = 0x0080;
inline constexpr std::uint16_t kL1 = 0x0100;
inline constexpr std::uint16_t kR1 = 0x0200;
inline constexpr std::uint16_t kL2 = 0x0400;
inline constexpr std::uint16_t kR2 = 0x0800;
inline constexpr std::uint16_t kCreate = 0x1000;
inline constexpr std::uint16_t kOptions = 0x2000;
inline constexpr std::uint16_t kL3 = 0x4000;
inline constexpr std::uint16_t kR3 = 0x8000;
} // namespace button

struct DualSenseInputState {
    std::uint8_t lx = 0x80;
    std::uint8_t ly = 0x80;
    std::uint8_t rx = 0x80;
    std::uint8_t ry = 0x80;
    std::uint8_t l2 = 0;
    std::uint8_t r2 = 0;
    std::uint8_t dpad = 0;
    std::uint16_t buttons = 0;
    std::uint16_t touch1X = 0;
    std::uint16_t touch1Y = 0;
    bool touch1Active = false;
    std::uint16_t touch2X = 0;
    std::uint16_t touch2Y = 0;
    bool touch2Active = false;
    std::uint8_t batteryPercent = 100;
    std::uint8_t chargeState = 0;

    bool operator==(const DualSenseInputState&) const = default;
};

std::array<std::uint8_t, 33> buildViiperInput(const DualSenseInputState& state);
std::array<std::uint8_t, 33> buildNeutralViiperInput();

} // namespace asb::dualsense
