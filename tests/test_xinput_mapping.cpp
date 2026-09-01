#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/XInputMapping.h"

#include <array>
#include <cassert>

int main() {
    using namespace asb;

    dualsense::DualSenseInputState state{};
    platform::mapXInputButtons(platform::xinputButton::kBack, 0, 0, state);
    assert(state.buttons == dualsense::button::kTouchpadClick);
    assert((state.buttons & dualsense::button::kCreate) == 0);

    platform::mapXInputButtons(platform::xinputButton::kStart, 0, 0, state);
    assert(state.buttons == dualsense::button::kOptions);

    constexpr std::uint16_t face = platform::xinputButton::kX |
                                   platform::xinputButton::kA |
                                   platform::xinputButton::kB |
                                   platform::xinputButton::kY;
    platform::mapXInputButtons(face, 0, 0, state);
    assert(state.buttons == (dualsense::button::kSquare |
                             dualsense::button::kCross |
                             dualsense::button::kCircle |
                             dualsense::button::kTriangle));

    constexpr std::uint16_t dpad = platform::xinputButton::kDpadUp |
                                   platform::xinputButton::kDpadLeft;
    platform::mapXInputButtons(dpad, 31, 31, state);
    assert(state.dpad == 0x05);
    assert(state.buttons == (dualsense::button::kL2 | dualsense::button::kR2));

    constexpr std::uint16_t shouldersAndThumbs =
        platform::xinputButton::kLeftShoulder |
        platform::xinputButton::kRightShoulder |
        platform::xinputButton::kLeftThumb |
        platform::xinputButton::kRightThumb;
    platform::mapXInputButtons(shouldersAndThumbs, 0, 0, state);
    assert(state.buttons == (dualsense::button::kL1 |
                             dualsense::button::kR1 |
                             dualsense::button::kL3 |
                             dualsense::button::kR3));

    // Capture-replay fixtures cover axis endpoints, independent triggers and
    // a large simultaneous-control state through the same complete mapping
    // function used by the physical proxy.
    const auto neutral = platform::mapXInputState({});
    assert(neutral.lx == 128 && neutral.ly == 127);
    assert(neutral.rx == 128 && neutral.ry == 127);
    assert(neutral.l2 == 0 && neutral.r2 == 0);

    platform::XInputSnapshot endpoints{};
    endpoints.leftX = -32768;
    endpoints.leftY = -32768;
    endpoints.rightX = 32767;
    endpoints.rightY = 32767;
    endpoints.leftTrigger = 1;
    endpoints.rightTrigger = 255;
    const auto extremes = platform::mapXInputState(endpoints);
    assert(extremes.lx == 0 && extremes.ly == 255);
    assert(extremes.rx == 255 && extremes.ry == 0);
    assert(extremes.l2 == 1 && extremes.r2 == 255);
    assert((extremes.buttons & dualsense::button::kL2) == 0);
    assert((extremes.buttons & dualsense::button::kR2) != 0);

    struct DpadCapture { std::uint16_t raw; std::uint8_t expected; };
    constexpr std::array<DpadCapture, 8> dpadCaptures{{
        {platform::xinputButton::kDpadUp, 0x01},
        {platform::xinputButton::kDpadUp | platform::xinputButton::kDpadRight, 0x09},
        {platform::xinputButton::kDpadRight, 0x08},
        {platform::xinputButton::kDpadDown | platform::xinputButton::kDpadRight, 0x0A},
        {platform::xinputButton::kDpadDown, 0x02},
        {platform::xinputButton::kDpadDown | platform::xinputButton::kDpadLeft, 0x06},
        {platform::xinputButton::kDpadLeft, 0x04},
        {platform::xinputButton::kDpadUp | platform::xinputButton::kDpadLeft, 0x05},
    }};
    for (const auto& capture : dpadCaptures) {
        platform::XInputSnapshot snapshot{};
        snapshot.buttons = capture.raw;
        assert(platform::mapXInputState(snapshot).dpad == capture.expected);
    }

    platform::XInputSnapshot simultaneous{};
    simultaneous.leftX = -12345;
    simultaneous.leftY = 23456;
    simultaneous.rightX = 32767;
    simultaneous.rightY = -32768;
    simultaneous.leftTrigger = 200;
    simultaneous.rightTrigger = 201;
    simultaneous.buttons = 0xFFFF;
    const auto allControls = platform::mapXInputState(simultaneous);
    assert(allControls.dpad == 0x0F);
    assert(allControls.buttons ==
           (dualsense::button::kTouchpadClick | dualsense::button::kOptions |
            dualsense::button::kSquare | dualsense::button::kCross |
            dualsense::button::kCircle | dualsense::button::kTriangle |
            dualsense::button::kL1 | dualsense::button::kR1 |
            dualsense::button::kL2 | dualsense::button::kR2 |
            dualsense::button::kL3 | dualsense::button::kR3));

    for (unsigned int transition = 0; transition < 10000; ++transition) {
        platform::XInputSnapshot snapshot{};
        snapshot.buttons = (transition & 1U) != 0 ? platform::xinputButton::kA : 0;
        const auto mapped = platform::mapXInputState(snapshot);
        assert(((mapped.buttons & dualsense::button::kCross) != 0) ==
               ((transition & 1U) != 0));
    }
    return 0;
}
