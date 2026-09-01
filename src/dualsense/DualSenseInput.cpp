#include "dualsense/DualSenseInput.h"

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

} // namespace asb::dualsense
