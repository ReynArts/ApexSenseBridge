#include "dualsense/DualSenseInput.h"

#include <algorithm>

namespace asb::dualsense {
namespace {

void writeI16(std::array<std::uint8_t, 33>& input, std::size_t offset,
              std::int16_t value) noexcept {
    const auto raw = static_cast<std::uint16_t>(value);
    input[offset] = static_cast<std::uint8_t>(raw & 0xFF);
    input[offset + 1] = static_cast<std::uint8_t>(raw >> 8);
}

} // namespace

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
    writeI16(input, 19, state.gyroX);
    writeI16(input, 21, state.gyroY);
    writeI16(input, 23, state.gyroZ);
    writeI16(input, 25, state.accelX);
    writeI16(input, 27, state.accelY);
    writeI16(input, 29, state.accelZ);
    input[31] = state.batteryPercent;
    input[32] = state.chargeState;
    return input;
}

std::array<std::uint8_t, 33> buildNeutralViiperInput() {
    return buildViiperInput(DualSenseInputState{});
}

std::uint8_t toBatteryPercent(std::uint8_t rawLevel) noexcept {
    if (rawLevel > 6) {
        return rawLevel > 100 ? static_cast<std::uint8_t>(100) : rawLevel;
    }
    switch (rawLevel) {
    case 6:
    case 5:
        return 100;
    case 4:
        return 80;
    case 3:
        return 60;
    case 2:
        return 40;
    case 1:
        return 20;
    case 0:
    default:
        return 10;
    }
}

std::uint8_t toChargeState(bool charging, bool wired, std::uint8_t batteryPercent) noexcept {
    if (charging || wired) {
        return batteryPercent >= 100
            ? chargeStatus::kFull
            : chargeStatus::kCharging;
    }
    return chargeStatus::kDischarging;
}

} // namespace asb::dualsense
