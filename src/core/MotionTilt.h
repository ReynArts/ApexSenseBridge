#pragma once

#include <cmath>

namespace asb {

struct MotionTilt {
    double pitchDegrees = 0.0;
    double rollDegrees = 0.0;
};

[[nodiscard]] inline MotionTilt motionTilt(bool sonyFrame, double ax, double ay,
                                           double az) noexcept {
    constexpr double kDegrees = 180.0 / 3.141592653589793;
    if (sonyFrame) {
        return {std::atan2(az, std::sqrt(ax * ax + ay * ay)) * kDegrees,
                std::atan2(ax, ay) * kDegrees};
    }
    return {std::atan2(-ay, std::sqrt(ax * ax + az * az)) * kDegrees,
            std::atan2(ax, az) * kDegrees};
}

} // namespace asb
