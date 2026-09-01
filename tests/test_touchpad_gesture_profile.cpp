#ifdef NDEBUG
#undef NDEBUG
#endif

#include "dualsense/TouchpadGestureProfile.h"

#include <cassert>
#include <chrono>

int main() {
    using namespace asb::dualsense;
    using namespace std::chrono_literals;
    const auto start = TouchpadGestureMapper::Clock::time_point{};

    assert(parseTouchpadGestureProfile("spider-man-2") ==
           TouchpadGestureProfile::SpiderMan2);
    assert(parseTouchpadGestureProfile("unknown") == std::nullopt);
    assert(touchpadGestureProfileName(TouchpadGestureProfile::GhostOfTsushima) ==
           "ghost-of-tsushima");

    // The safe fallback does not invent any gesture or delay View.
    TouchpadGestureMapper none;
    DualSenseInputState input{};
    input.buttons = button::kTouchpadClick;
    none.transform(input, start);
    assert((input.buttons & button::kTouchpadClick) != 0);
    assert(!input.touch1Active);

    // Miles preserves View tap as touchpad click and maps View hold to the
    // documented right-to-left FNSM swipe.
    TouchpadGestureMapper miles(TouchpadGestureProfile::MilesMorales);
    input = {};
    input.buttons = button::kTouchpadClick;
    miles.transform(input, start);
    assert((input.buttons & button::kTouchpadClick) == 0);

    input = {};
    miles.transform(input, start + 100ms);
    assert((input.buttons & button::kTouchpadClick) != 0);
    assert(input.touch1Active && input.touch1X == 960 && input.touch1Y == 540);

    input = {};
    miles.transform(input, start + 140ms);
    assert((input.buttons & button::kTouchpadClick) == 0);
    assert(!input.touch1Active);

    const auto milesHold = start + 1s;
    input = {};
    input.buttons = button::kTouchpadClick;
    miles.transform(input, milesHold);
    input = {};
    input.buttons = button::kTouchpadClick;
    miles.transform(input, milesHold + 400ms);
    assert(input.touch1Active);
    assert(input.touch1X == 1600 && input.touch1Y == 540);
    input = {};
    input.buttons = button::kTouchpadClick;
    miles.transform(input, milesHold + 490ms);
    assert(input.touch1Active && input.touch1X < 1600 && input.touch1X > 320);
    assert(miles.stats().swipesByDirection[
               static_cast<std::size_t>(TouchpadSwipeDirection::Left)] == 1);

    // Spider-Man 2 has two distinct holds: View -> left/FNSM and D-pad Up ->
    // camera toggle. Xbox uses the same D-pad button to open and close it, so
    // successive holds must alternate DualSense up/down swipes. A short D-pad
    // press is replayed instead of being lost.
    TouchpadGestureMapper spider(TouchpadGestureProfile::SpiderMan2);
    input = {};
    input.dpad = 0x01;
    spider.transform(input, start);
    assert((input.dpad & 0x01U) == 0);
    input = {};
    spider.transform(input, start + 50ms);
    assert((input.dpad & 0x01U) != 0);

    const auto cameraHold = start + 1s;
    input = {};
    input.dpad = 0x01;
    spider.transform(input, cameraHold);
    input = {};
    input.dpad = 0x01;
    spider.transform(input, cameraHold + 400ms);
    assert((input.dpad & 0x01U) == 0);
    assert(input.touch1Active && input.touch1X == 960 && input.touch1Y == 850);
    assert(spider.stats().swipesByDirection[
               static_cast<std::size_t>(TouchpadSwipeDirection::Up)] == 1);

    input = {};
    spider.transform(input, cameraHold + 600ms);

    const auto cameraCloseHold = cameraHold + 1s;
    input = {};
    input.dpad = 0x01;
    spider.transform(input, cameraCloseHold);
    input = {};
    input.dpad = 0x01;
    spider.transform(input, cameraCloseHold + 400ms);
    assert((input.dpad & 0x01U) == 0);
    assert(input.touch1Active && input.touch1X == 960 && input.touch1Y == 200);
    assert(spider.stats().swipesByDirection[
               static_cast<std::size_t>(TouchpadSwipeDirection::Down)] == 1);

    // Ghost recognizes only the official modifier layer. It consumes both the
    // modifier and gesture stick so the DualSense game receives one swipe.
    TouchpadGestureMapper ghost(TouchpadGestureProfile::GhostOfTsushima);
    input = {};
    input.dpad = 0x08;
    ghost.transform(input, start);
    assert((input.dpad & 0x08U) == 0);
    input = {};
    input.dpad = 0x08;
    input.rx = 0x80;
    input.ry = 0x00;
    ghost.transform(input, start + 30ms);
    assert((input.dpad & 0x08U) == 0);
    assert(input.rx == 0x80 && input.ry == 0x80);
    assert(input.touch1Active && input.touch1Y == 850);
    assert(ghost.stats().swipesByDirection[
               static_cast<std::size_t>(TouchpadSwipeDirection::Up)] == 1);

    TouchpadGestureMapper ghostTap(TouchpadGestureProfile::GhostOfTsushima);
    input = {};
    input.dpad = 0x08;
    ghostTap.transform(input, start);
    input = {};
    ghostTap.transform(input, start + 20ms);
    assert((input.dpad & 0x08U) != 0);
    assert(ghostTap.stats().replayedTaps == 1);

    // Warframe requires the modifier to lead the face button by at least one
    // report; RB+A becomes power 1 / swipe up and neither control leaks.
    TouchpadGestureMapper warframe(TouchpadGestureProfile::Warframe);
    input = {};
    input.buttons = button::kR1;
    warframe.transform(input, start);
    assert((input.buttons & button::kR1) == 0);
    input = {};
    input.buttons = static_cast<std::uint16_t>(button::kR1 | button::kCross);
    warframe.transform(input, start + 30ms);
    assert((input.buttons & (button::kR1 | button::kCross)) == 0);
    assert(input.touch1Active && input.touch1Y == 850);
    assert(warframe.stats().swipesByDirection[
               static_cast<std::size_t>(TouchpadSwipeDirection::Up)] == 1);

    TouchpadGestureMapper warframeSimultaneous(TouchpadGestureProfile::Warframe);
    input = {};
    input.buttons = static_cast<std::uint16_t>(button::kR1 | button::kCross);
    warframeSimultaneous.transform(input, start);
    assert(!input.touch1Active);
    assert((input.buttons & button::kR1) != 0);
    assert((input.buttons & button::kCross) != 0);
    assert(warframeSimultaneous.stats().swipes == 0);
    input = {};
    warframeSimultaneous.transform(input, start + 30ms);
    assert(input.buttons == 0);
    assert(warframeSimultaneous.stats().replayedTaps == 0);

    return 0;
}
