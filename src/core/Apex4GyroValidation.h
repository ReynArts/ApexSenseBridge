#pragma once

#include "dualsense/DualSenseInput.h"

#include <chrono>

namespace asb {

// APEX 4 reports an all-zero IMU unless the Space Station profile maps gyro to "Mouse, always on".
class Apex4GyroValidation {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto kQuietPeriod = std::chrono::seconds(5);

    // Returns true exactly once, when only zero motion was seen for kQuietPeriod.
    [[nodiscard]] bool observe(const dualsense::DualSenseInputState& state,
                               Clock::time_point now) noexcept {
        if (done_) return false;
        if (state.gyroX != 0 || state.gyroY != 0 || state.gyroZ != 0 ||
            state.accelX != 0 || state.accelY != 0 || state.accelZ != 0) {
            done_ = true;
            return false;
        }
        if (!started_) {
            started_ = true;
            firstZeroAt_ = now;
            return false;
        }
        if (now - firstZeroAt_ < kQuietPeriod) return false;
        done_ = true;
        return true;
    }

private:
    Clock::time_point firstZeroAt_{};
    bool started_ = false;
    bool done_ = false;
};

} // namespace asb
