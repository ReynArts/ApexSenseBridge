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
    return 0;
}
