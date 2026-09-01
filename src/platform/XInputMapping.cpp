#include "platform/XInputMapping.h"

namespace asb::platform {

namespace {

std::uint8_t axisToByte(std::int16_t value, bool invert) noexcept {
    const auto shifted = static_cast<std::uint32_t>(static_cast<std::int32_t>(value) + 32768);
    auto result = static_cast<std::uint8_t>((shifted * 255U + 32767U) / 65535U);
    return invert ? static_cast<std::uint8_t>(255U - result) : result;
}

} // namespace

void mapXInputButtons(std::uint16_t raw,
                      std::uint8_t leftTrigger,
                      std::uint8_t rightTrigger,
                      dualsense::DualSenseInputState& state) noexcept {
    using namespace xinputButton;
    using namespace dualsense::button;

    state.dpad = 0;
    state.buttons = 0;
    if (raw & kDpadUp) state.dpad |= 0x01;
    if (raw & kDpadDown) state.dpad |= 0x02;
    if (raw & kDpadLeft) state.dpad |= 0x04;
    if (raw & kDpadRight) state.dpad |= 0x08;
    if (raw & kX) state.buttons |= kSquare;
    if (raw & kA) state.buttons |= kCross;
    if (raw & kB) state.buttons |= kCircle;
    if (raw & kY) state.buttons |= kTriangle;
    if (raw & kLeftShoulder) state.buttons |= kL1;
    if (raw & kRightShoulder) state.buttons |= kR1;
    if (leftTrigger > kTriggerThreshold) state.buttons |= kL2;
    if (rightTrigger > kTriggerThreshold) state.buttons |= kR2;

    // XInput has no touchpad. View/Back is the only practical source for the
    // DualSense touchpad click used as Map in Spider-Man and many native games.
    if (raw & kBack) state.buttons |= kTouchpadClick;
    if (raw & kStart) state.buttons |= kOptions;
    if (raw & kLeftThumb) state.buttons |= kL3;
    if (raw & kRightThumb) state.buttons |= kR3;
}

dualsense::DualSenseInputState mapXInputState(
    const XInputSnapshot& snapshot) noexcept {
    dualsense::DualSenseInputState converted{};
    converted.lx = axisToByte(snapshot.leftX, false);
    converted.ly = axisToByte(snapshot.leftY, true);
    converted.rx = axisToByte(snapshot.rightX, false);
    converted.ry = axisToByte(snapshot.rightY, true);
    converted.l2 = snapshot.leftTrigger;
    converted.r2 = snapshot.rightTrigger;
    mapXInputButtons(snapshot.buttons, snapshot.leftTrigger,
                     snapshot.rightTrigger, converted);
    return converted;
}

} // namespace asb::platform
