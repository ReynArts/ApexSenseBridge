#ifdef NDEBUG
#undef NDEBUG
#endif

#include "dualsense/ViewTouchpadGesture.h"

#include <cassert>
#include <chrono>

int main() {
    using namespace asb::dualsense;
    using namespace std::chrono_literals;

    ViewTouchpadGesture gesture;
    const auto start = ViewTouchpadGesture::Clock::time_point{};

    DualSenseInputState input{};
    input.buttons = static_cast<std::uint16_t>(button::kTouchpadClick | button::kCross);
    gesture.transform(input, start);
    assert((input.buttons & button::kTouchpadClick) == 0);
    assert((input.buttons & button::kCross) != 0);
    assert(!input.touch1Active);

    input = {};
    gesture.transform(input, start + 120ms);
    assert((input.buttons & button::kTouchpadClick) != 0);
    assert(input.touch1Active);
    assert(gesture.stats().taps == 1);
    assert(gesture.stats().swipes == 0);

    input = {};
    gesture.transform(input, start + 160ms);
    assert((input.buttons & button::kTouchpadClick) == 0);
    assert(!input.touch1Active);

    const auto secondPress = start + 1s;
    input = {};
    input.buttons = button::kTouchpadClick;
    gesture.transform(input, secondPress);
    assert(!input.touch1Active);

    input = {};
    input.buttons = button::kTouchpadClick;
    gesture.transform(input, secondPress + 399ms);
    assert(!input.touch1Active);

    input = {};
    input.buttons = button::kTouchpadClick;
    gesture.transform(input, secondPress + 400ms);
    assert((input.buttons & button::kTouchpadClick) == 0);
    assert(input.touch1Active);
    assert(input.touch1X == 960);
    assert(input.touch1Y == 850);
    assert(gesture.stats().swipes == 1);

    input = {};
    input.buttons = button::kTouchpadClick;
    gesture.transform(input, secondPress + 490ms);
    assert(input.touch1Active);
    assert(input.touch1Y < 850 && input.touch1Y > 200);

    input = {};
    input.buttons = button::kTouchpadClick;
    gesture.transform(input, secondPress + 580ms);
    assert(!input.touch1Active);

    input = {};
    gesture.transform(input, secondPress + 600ms);
    assert(!input.touch1Active);
    assert((input.buttons & button::kTouchpadClick) == 0);
    assert(gesture.stats().taps == 1);
    assert(gesture.stats().swipes == 1);
    return 0;
}
