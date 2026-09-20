#include "dualsense/DualSenseInput.h"

#include <cstddef>

namespace asb::dualsense {

std::array<std::uint8_t, 33> buildViiperInput(const DualSenseInputState& state) {
    std::array<std::uint8_t, 33> input{};
    input[0] = state.lx;
    input[1] = state.ly;
    input[2] = state.rx;
    input[3] = state.ry;
    input[4] = state.l2;
    input[5] = state.r2;
    input[6] = state.dpad;
    input[7] = static_cast<std::uint8_t>(state.buttons & 0xFF);
    input[8] = static_cast<std::uint8_t>(state.buttons >> 8);
    input[9] = static_cast<std::uint8_t>(state.touch1X & 0xFF);
    input[10] = static_cast<std::uint8_t>(state.touch1X >> 8);
    input[11] = static_cast<std::uint8_t>(state.touch1Y & 0xFF);
    input[12] = static_cast<std::uint8_t>(state.touch1Y >> 8);
    input[13] = state.touch1Active ? 1 : 0;
    input[14] = static_cast<std::uint8_t>(state.touch2X & 0xFF);
    input[15] = static_cast<std::uint8_t>(state.touch2X >> 8);
    input[16] = static_cast<std::uint8_t>(state.touch2Y & 0xFF);
    input[17] = static_cast<std::uint8_t>(state.touch2Y >> 8);
    input[18] = state.touch2Active ? 1 : 0;
    input[31] = state.batteryPercent;
    input[32] = state.chargeState;
    return input;
}

std::array<std::uint8_t, 33> buildNeutralViiperInput() {
    return buildViiperInput(DualSenseInputState{});
}

namespace {

// USB hat encoding: 0 = up, clockwise to 7 = up-left, 8 = neutral.
constexpr std::uint8_t kHatUp = 0x00;
constexpr std::uint8_t kHatUpRight = 0x01;
constexpr std::uint8_t kHatRight = 0x02;
constexpr std::uint8_t kHatDownRight = 0x03;
constexpr std::uint8_t kHatDown = 0x04;
constexpr std::uint8_t kHatDownLeft = 0x05;
constexpr std::uint8_t kHatLeft = 0x06;
constexpr std::uint8_t kHatUpLeft = 0x07;
constexpr std::uint8_t kHatNeutral = 0x08;

constexpr std::uint8_t kDpadUp = 0x01;
constexpr std::uint8_t kDpadDown = 0x02;
constexpr std::uint8_t kDpadLeft = 0x04;
constexpr std::uint8_t kDpadRight = 0x08;

constexpr std::uint8_t kTouchInactiveMask = 0x80;
constexpr std::uint16_t kTouchpadMaxX = 1919;
constexpr std::uint16_t kTouchpadMaxY = 1069;

constexpr std::uint8_t kConnectionStateUsb = 0x08;

// A DualSense reports 8192 counts per g, which is the scale the calibration
// feature report this bridge serves declares. The APEX has no motion sensors to
// forward, but reporting zeros on all three accelerometer axes describes free
// fall: a state no real controller is ever in, and one that leaves any host
// deriving an orientation from a zero-length gravity vector. Reporting a
// controller resting level is both honest and stable.
constexpr std::int16_t kAccelerometerCountsPerG = 8192;
constexpr std::uint8_t kBatteryStatusDischarging = 0x00;
constexpr std::uint8_t kBatteryStatusCharging = 0x01;
constexpr std::uint8_t kBatteryStatusFull = 0x02;

std::uint8_t dpadToHat(std::uint8_t dpad) noexcept {
    const bool up = (dpad & kDpadUp) != 0;
    const bool down = (dpad & kDpadDown) != 0;
    const bool left = (dpad & kDpadLeft) != 0;
    const bool right = (dpad & kDpadRight) != 0;
    if (up && right) return kHatUpRight;
    if (up && left) return kHatUpLeft;
    if (down && right) return kHatDownRight;
    if (down && left) return kHatDownLeft;
    if (up) return kHatUp;
    if (down) return kHatDown;
    if (left) return kHatLeft;
    if (right) return kHatRight;
    return kHatNeutral;
}

std::uint8_t faceButtonsNibble(std::uint16_t buttons) noexcept {
    std::uint8_t face = 0;
    if ((buttons & button::kSquare) != 0) face |= 0x01;
    if ((buttons & button::kCross) != 0) face |= 0x02;
    if ((buttons & button::kCircle) != 0) face |= 0x04;
    if ((buttons & button::kTriangle) != 0) face |= 0x08;
    return static_cast<std::uint8_t>(face << 4);
}

std::uint8_t shoulderButtonsByte(std::uint16_t buttons) noexcept {
    std::uint8_t out = 0;
    if ((buttons & button::kL1) != 0) out |= 0x01;
    if ((buttons & button::kR1) != 0) out |= 0x02;
    if ((buttons & button::kL2) != 0) out |= 0x04;
    if ((buttons & button::kR2) != 0) out |= 0x08;
    if ((buttons & button::kCreate) != 0) out |= 0x10;
    if ((buttons & button::kOptions) != 0) out |= 0x20;
    if ((buttons & button::kL3) != 0) out |= 0x40;
    if ((buttons & button::kR3) != 0) out |= 0x80;
    return out;
}

std::uint8_t systemButtonsByte(std::uint16_t buttons) noexcept {
    std::uint8_t out = 0;
    if ((buttons & button::kPs) != 0) out |= 0x01;
    if ((buttons & button::kTouchpadClick) != 0) out |= 0x02;
    if ((buttons & button::kMute) != 0) out |= 0x04;
    return out;
}

void writeI16(std::array<std::uint8_t, kUsbInputReportSize>& report,
              std::size_t offset,
              std::int16_t value) noexcept {
    const auto raw = static_cast<std::uint16_t>(value);
    report[offset] = static_cast<std::uint8_t>(raw & 0xFF);
    report[offset + 1] = static_cast<std::uint8_t>((raw >> 8) & 0xFF);
}

void writeU32(std::array<std::uint8_t, kUsbInputReportSize>& report,
              std::size_t offset,
              std::uint32_t value) noexcept {
    report[offset] = static_cast<std::uint8_t>(value & 0xFF);
    report[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    report[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    report[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

// Touch coordinates are packed as 12 bits of X followed by 12 bits of Y.
void encodeTouchCoords(std::array<std::uint8_t, kUsbInputReportSize>& report,
                       std::size_t offset,
                       std::uint16_t x,
                       std::uint16_t y) noexcept {
    if (x > kTouchpadMaxX) x = kTouchpadMaxX;
    if (y > kTouchpadMaxY) y = kTouchpadMaxY;
    report[offset] = static_cast<std::uint8_t>(x & 0xFF);
    report[offset + 1] = static_cast<std::uint8_t>(((x >> 8) & 0x0F) |
                                                   ((y & 0x0F) << 4));
    report[offset + 2] = static_cast<std::uint8_t>(y >> 4);
}

std::uint8_t batteryByte(std::uint8_t levelPercent, std::uint8_t chargeState) noexcept {
    std::uint8_t level = static_cast<std::uint8_t>(levelPercent / 10);
    if (level > 10) level = 10;
    std::uint8_t status = kBatteryStatusDischarging;
    if (chargeState == 2) {
        status = kBatteryStatusCharging;
    } else if (chargeState == 4) {
        status = kBatteryStatusFull;
    }
    return static_cast<std::uint8_t>((status << 4) | (level & 0x0F));
}

} // namespace

void DualSenseUsbReportCounters::advance(std::uint32_t sensorTicks) noexcept {
    ++sequence;
    ++packetSequence;
    sensorTimestamp += sensorTicks;
}

std::array<std::uint8_t, kUsbInputReportSize> buildDualSenseUsbInputReport(
    const DualSenseInputState& state,
    const DualSenseUsbReportCounters& counters) {
    std::array<std::uint8_t, kUsbInputReportSize> report{};
    report[0] = kUsbInputReportId;
    report[1] = state.lx;
    report[2] = state.ly;
    report[3] = state.rx;
    report[4] = state.ry;
    report[5] = state.l2;
    report[6] = state.r2;
    report[7] = counters.sequence;

    report[8] = static_cast<std::uint8_t>(dpadToHat(state.dpad) |
                                          faceButtonsNibble(state.buttons));
    report[9] = shoulderButtonsByte(state.buttons);
    report[10] = systemButtonsByte(state.buttons);
    report[11] = 0;

    writeU32(report, 12, counters.packetSequence);
    // Bytes 16..21 are the gyroscope: zero is correct, the pad is not turning.
    // Bytes 22..27 are the accelerometer, where zero would not be: gravity has
    // to point somewhere, so report the controller lying level.
    writeI16(report, 26, kAccelerometerCountsPerG);
    writeU32(report, 28, counters.sensorTimestamp);
    report[32] = 0;

    report[33] = static_cast<std::uint8_t>(counters.touch1Tracking & 0x7F);
    if (!state.touch1Active) report[33] |= kTouchInactiveMask;
    encodeTouchCoords(report, 34, state.touch1X, state.touch1Y);

    report[37] = static_cast<std::uint8_t>(counters.touch2Tracking & 0x7F);
    if (!state.touch2Active) report[37] |= kTouchInactiveMask;
    encodeTouchCoords(report, 38, state.touch2X, state.touch2Y);

    writeU32(report, 49, counters.sensorTimestamp);
    report[53] = batteryByte(state.batteryPercent, state.chargeState);
    report[54] = kConnectionStateUsb;
    return report;
}

} // namespace asb::dualsense
