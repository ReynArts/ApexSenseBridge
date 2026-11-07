#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/Apex6InputFusion.h"

#include "dualsense/DualSenseInput.h"

#include <cassert>
#include <chrono>
#include <optional>

int main() {
    using namespace std::chrono_literals;
    using asb::dualsense::DualSenseInputState;
    using asb::platform::Apex6InputFusion;

    Apex6InputFusion fusion(250ms);
    const auto start = Apex6InputFusion::Clock::time_point{} + 1s;

    DualSenseInputState xinput{};
    xinput.lx = 10;
    xinput.buttons = asb::dualsense::button::kCircle;
    xinput.l2 = 40;
    xinput.r2 = 41;

    const auto bootstrap = fusion.observe(std::nullopt, xinput, start);
    assert(bootstrap);
    assert(bootstrap->lx == 10);
    assert(bootstrap->l2 == 40 && bootstrap->r2 == 41);

    DualSenseInputState mapped{};
    mapped.lx = 99;
    mapped.ry = 77;
    mapped.buttons = asb::dualsense::button::kCross;
    const auto mappedResult = fusion.observe(mapped, xinput, start + 1ms);
    assert(mappedResult);
    assert(mappedResult->lx == 99 && mappedResult->ry == 77);
    assert((mappedResult->buttons & asb::dualsense::button::kCross) != 0);
    assert((mappedResult->buttons & asb::dualsense::button::kCircle) == 0);
    assert(mappedResult->l2 == 40 && mappedResult->r2 == 41);

    // Once HID is authoritative, unrelated XInput controls are ignored.
    xinput.lx = 200;
    xinput.buttons = asb::dualsense::button::kTriangle;
    assert(!fusion.observe(std::nullopt, xinput, start + 100ms));

    // Trigger-only changes are published without waiting for another HID report.
    xinput.l2 = 200;
    xinput.r2 = 201;
    const auto triggers = fusion.observe(std::nullopt, xinput, start + 101ms);
    assert(triggers);
    assert(triggers->lx == 99);
    assert(triggers->l2 == 200 && triggers->r2 == 201);
    assert((triggers->buttons & asb::dualsense::button::kL2) != 0);
    assert((triggers->buttons & asb::dualsense::button::kR2) != 0);

    assert(!fusion.observe(std::nullopt, xinput, start + 350ms));
    const auto heartbeat = fusion.observe(std::nullopt, xinput, start + 351ms);
    assert(heartbeat);
    assert(*heartbeat == *triggers);

    Apex6InputFusion motionFusion(250ms);
    DualSenseInputState motionMapped{};
    motionMapped.lx = 42;
    motionMapped.gyroX = 5;
    DualSenseInputState motionXInput{};
    const auto motionStart = Apex6InputFusion::Clock::time_point{} + 5s;

    asb::flydigi::apex6::MotionSample sample{};
    sample.gyroX = 100;
    sample.gyroY = -200;
    sample.gyroZ = 300;
    sample.accelX = 1;
    sample.accelY = 10000;
    sample.accelZ = -3;
    const auto withMotion = motionFusion.observe(
        motionMapped, motionXInput, sample, motionStart);
    assert(withMotion);
    assert(withMotion->lx == 42);
    assert(withMotion->gyroX == 100 && withMotion->gyroY == -200 && withMotion->gyroZ == 300);
    assert(withMotion->accelX == 1 && withMotion->accelY == 10000 && withMotion->accelZ == -3);

    assert(!motionFusion.observe(std::nullopt, motionXInput, sample, motionStart + 10ms));

    sample.gyroX = 101;
    const auto motionOnly = motionFusion.observe(
        std::nullopt, motionXInput, sample, motionStart + 11ms);
    assert(motionOnly);
    assert(motionOnly->lx == 42 && motionOnly->gyroX == 101);

    const asb::flydigi::apex6::MotionSample neutral{};
    const auto stale = motionFusion.observe(
        std::nullopt, motionXInput, neutral, motionStart + 12ms);
    assert(stale);
    assert(stale->gyroX == 0 && stale->accelY == 0);

    const auto noMotion = motionFusion.observe(
        motionMapped, motionXInput, std::nullopt, motionStart + 13ms);
    assert(noMotion);
    assert(noMotion->gyroX == 5);
    return 0;
}
