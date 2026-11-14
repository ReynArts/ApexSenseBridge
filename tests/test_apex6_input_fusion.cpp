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

    using asb::platform::Apex6VendorTriggers;
    const auto vendor = [](std::uint8_t left, std::uint8_t right,
                           Apex6InputFusion::Clock::time_point at) {
        return std::optional<Apex6VendorTriggers>(Apex6VendorTriggers{left, right, at});
    };
    const auto noMotionSample = std::optional<asb::flydigi::apex6::MotionSample>{};
    const auto base = Apex6InputFusion::Clock::time_point{} + 10s;

    {
        Apex6InputFusion fse(250ms);
        DualSenseInputState idle{};
        DualSenseInputState neutralXInput{};
        DualSenseInputState leftHeld{};
        leftHeld.l2 = 200;
        leftHeld.buttons = asb::dualsense::button::kL2;

        auto result = fse.observe(idle, neutralXInput, noMotionSample, std::nullopt, base);
        assert(result && result->l2 == 0 && !fse.fallbackActive());

        result = fse.observe(leftHeld, neutralXInput, noMotionSample, std::nullopt, base + 10ms);
        assert(result && result->l2 == 0);
        assert(!fse.fallbackActive());

        result = fse.observe(std::nullopt, neutralXInput, noMotionSample, std::nullopt,
                             base + 100ms);
        assert(!fse.fallbackActive());

        result = fse.observe(std::nullopt, neutralXInput, noMotionSample, std::nullopt,
                             base + 131ms);
        assert(fse.fallbackActive());
        assert(result && result->l2 == 200 && result->r2 == 0);
        assert((result->buttons & asb::dualsense::button::kL2) != 0);
        assert(fse.triggerStats().fallbackSwitches == 1);
        assert(fse.triggerStats().mappedMaxL2 == 200);
        assert(fse.triggerStats().xinputMaxL2 == 0);

        DualSenseInputState rightHeld{};
        rightHeld.r2 = 180;
        rightHeld.buttons = asb::dualsense::button::kR2;
        result = fse.observe(rightHeld, neutralXInput, noMotionSample, std::nullopt,
                             base + 200ms);
        assert(result && result->l2 == 0 && result->r2 == 180);
        assert((result->buttons & asb::dualsense::button::kR2) != 0);
        assert((result->buttons & asb::dualsense::button::kL2) == 0);

        result = fse.observe(idle, neutralXInput, noMotionSample, std::nullopt, base + 300ms);
        assert(fse.fallbackActive());
        assert(result && result->l2 == 0 && result->r2 == 0);
        assert((result->buttons & (asb::dualsense::button::kL2 | asb::dualsense::button::kR2)) == 0);

        DualSenseInputState revived{};
        revived.l2 = 90;
        result = fse.observe(idle, revived, noMotionSample, std::nullopt, base + 400ms);
        assert(!fse.fallbackActive());
        assert(result && result->l2 == 90 && result->r2 == 0);
        assert(fse.triggerStats().fallbackSwitches == 2);
        assert(fse.triggerStats().xinputMaxL2 == 90);
        assert(fse.triggerStats().fallbackUsed);
    }

    {
        Apex6InputFusion transient(250ms);
        DualSenseInputState neutralXInput{};
        DualSenseInputState mappedPress{};
        mappedPress.l2 = 120;
        DualSenseInputState livePress{};
        livePress.l2 = 118;

        (void)transient.observe(mappedPress, neutralXInput, noMotionSample, std::nullopt, base);
        (void)transient.observe(std::nullopt, neutralXInput, noMotionSample, std::nullopt,
                                base + 60ms);
        const auto caughtUp = transient.observe(
            std::nullopt, livePress, noMotionSample, std::nullopt, base + 70ms);
        assert(!transient.fallbackActive());
        assert(caughtUp && caughtUp->l2 == 118);
        (void)transient.observe(std::nullopt, neutralXInput, noMotionSample, std::nullopt,
                                base + 120ms);
        assert(!transient.fallbackActive());
        (void)transient.observe(std::nullopt, livePress, noMotionSample, std::nullopt,
                                base + 400ms);
        assert(!transient.fallbackActive());
        assert(transient.triggerStats().fallbackSwitches == 0);
    }

    {
        Apex6InputFusion both(250ms);
        DualSenseInputState neutralXInput{};
        DualSenseInputState netZero{};
        DualSenseInputState leftOnly{};
        leftOnly.l2 = 255;
        leftOnly.buttons = asb::dualsense::button::kL2;

        (void)both.observe(leftOnly, neutralXInput, noMotionSample, vendor(255, 0, base), base);
        (void)both.observe(std::nullopt, neutralXInput, noMotionSample,
                           vendor(255, 0, base + 100ms), base + 100ms);
        auto result = both.observe(std::nullopt, neutralXInput, noMotionSample,
                                   vendor(255, 0, base + 140ms), base + 140ms);
        assert(both.fallbackActive());
        assert(result && result->l2 == 255 && result->r2 == 0);

        result = both.observe(netZero, neutralXInput, noMotionSample,
                              vendor(255, 6, base + 150ms), base + 150ms);
        assert(result && result->l2 == 0 && result->r2 == 0);

        result = both.observe(std::nullopt, neutralXInput, noMotionSample,
                              vendor(255, 255, base + 160ms), base + 160ms);
        assert(result && result->l2 == 255 && result->r2 == 255);
        assert((result->buttons & asb::dualsense::button::kL2) != 0);
        assert((result->buttons & asb::dualsense::button::kR2) != 0);

        result = both.observe(std::nullopt, neutralXInput, noMotionSample,
                              vendor(255, 5, base + 170ms), base + 170ms);
        assert(result && result->l2 == 255 && result->r2 == 5);

        result = both.observe(std::nullopt, neutralXInput, noMotionSample,
                              vendor(255, 3, base + 180ms), base + 180ms);
        assert(result && result->l2 == 0 && result->r2 == 0);

        result = both.observe(std::nullopt, neutralXInput, noMotionSample,
                              vendor(255, 255, base + 190ms), base + 190ms);
        assert(result && result->l2 == 255 && result->r2 == 255);

        result = both.observe(std::nullopt, neutralXInput, noMotionSample,
                              vendor(255, 255, base + 190ms), base + 400ms);
        assert(result && result->l2 == 0 && result->r2 == 0);
        assert(both.triggerStats().vendorMaxL2 == 255 && both.triggerStats().vendorMaxR2 == 255);
        assert(both.triggerStats().fallbackReports >= 5);
    }

    {
        Apex6InputFusion hidOnly(250ms);
        hidOnly.setXInputAvailable(false);
        DualSenseInputState neutralXInput{};
        assert(hidOnly.fallbackActive());
        assert(!hidOnly.observe(std::nullopt, neutralXInput, noMotionSample, std::nullopt, base));

        DualSenseInputState mappedLeft{};
        mappedLeft.lx = 128;
        mappedLeft.l2 = 77;
        mappedLeft.buttons = asb::dualsense::button::kL2;
        auto result = hidOnly.observe(mappedLeft, neutralXInput, noMotionSample, std::nullopt,
                                      base + 1ms);
        assert(result && result->lx == 128 && result->l2 == 77 && result->r2 == 0);
        assert(hidOnly.triggerStats().fallbackUsed);

        result = hidOnly.observe(std::nullopt, neutralXInput, noMotionSample,
                                 vendor(200, 190, base + 5ms), base + 6ms);
        assert(result && result->l2 == 200 && result->r2 == 190);
        assert(hidOnly.fallbackActive());
    }
    return 0;
}
