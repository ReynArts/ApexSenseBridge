#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/Apex4GyroValidation.h"
#include "dualsense/DualSenseInput.h"

#include <cassert>
#include <chrono>

int main() {
    using namespace std::chrono_literals;
    const asb::Apex4GyroValidation::Clock::time_point t0{};
    const asb::dualsense::DualSenseInputState zero{};

    {
        asb::Apex4GyroValidation validation;
        assert(!validation.observe(zero, t0));
        assert(!validation.observe(zero, t0 + 4999ms));
        assert(validation.observe(zero, t0 + 5s));
        assert(!validation.observe(zero, t0 + 10s));
    }
    {
        asb::Apex4GyroValidation validation;
        auto moving = zero;
        moving.gyroX = 100;
        assert(!validation.observe(moving, t0));
        assert(!validation.observe(zero, t0 + 10s));
    }
    {
        asb::Apex4GyroValidation validation;
        assert(!validation.observe(zero, t0));
        auto gravity = zero;
        gravity.accelY = 500;
        assert(!validation.observe(gravity, t0 + 2s));
        assert(!validation.observe(zero, t0 + 20s));
    }
    return 0;
}
