#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/MotionTilt.h"

#include <array>
#include <cassert>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double radians(double degrees) { return degrees * kPi / 180.0; }

bool near(double value, double expected, double tolerance) {
    return std::abs(value - expected) <= tolerance;
}

void testFlatSonyFrameIsLevel() {
    const auto tilt = asb::motionTilt(true, 0.0, 1.0, 0.0);
    assert(near(tilt.pitchDegrees, 0.0, 1e-9));
    assert(near(tilt.rollDegrees, 0.0, 1e-9));

    const auto resting = asb::motionTilt(true, -0.02, 1.0, 0.08);
    assert(std::abs(resting.pitchDegrees) < 5.0);
    assert(std::abs(resting.rollDegrees) < 5.0);
}

void testSmallTiltGivesSmallAngle() {
    constexpr std::array<double, 4> kDegrees{1.0, 2.5, 5.0, 10.0};
    for (const double degrees : kDegrees) {
        const double angle = radians(degrees);
        const auto roll = asb::motionTilt(true, -std::sin(angle), std::cos(angle), 0.0);
        assert(near(roll.rollDegrees, -degrees, 0.01));
        assert(near(roll.pitchDegrees, 0.0, 0.01));
        const auto pitch = asb::motionTilt(true, 0.0, std::cos(angle), -std::sin(angle));
        assert(near(pitch.pitchDegrees, -degrees, 0.01));
        assert(near(pitch.rollDegrees, 0.0, 0.01));
    }
}

void testTiltsFollowTheSensorAxes() {
    const double angle = radians(45.0);
    const auto frontUp = asb::motionTilt(true, 0.0, std::cos(angle), -std::sin(angle));
    assert(near(frontUp.pitchDegrees, -45.0, 0.01));
    const auto rightDown = asb::motionTilt(true, -std::sin(angle), std::cos(angle), 0.0);
    assert(near(rightDown.rollDegrees, -45.0, 0.01));
    const auto leftDown = asb::motionTilt(true, std::sin(angle), std::cos(angle), 0.0);
    assert(near(leftDown.rollDegrees, 45.0, 0.01));
}

void testTurnedOverIsDetected() {
    const auto flipped = asb::motionTilt(true, 0.01, -1.0, 0.02);
    assert(std::abs(flipped.rollDegrees) > 170.0);
    const auto flippedOtherWay = asb::motionTilt(true, -0.01, -1.0, 0.02);
    assert(std::abs(flippedOtherWay.rollDegrees) > 170.0);
    assert(flipped.rollDegrees * flippedOtherWay.rollDegrees < 0.0);
}

void testOtherFramesKeepTheirFormula() {
    const auto flat = asb::motionTilt(false, 0.0, 0.0, 1.0);
    assert(near(flat.pitchDegrees, 0.0, 1e-9));
    assert(near(flat.rollDegrees, 0.0, 1e-9));
    const auto banked = asb::motionTilt(false, 0.5, 0.0, std::sqrt(0.75));
    assert(near(banked.rollDegrees, 30.0, 0.01));
}

void testDegenerateInputIsFinite() {
    const auto zero = asb::motionTilt(true, 0.0, 0.0, 0.0);
    assert(std::isfinite(zero.pitchDegrees) && std::isfinite(zero.rollDegrees));
}

} // namespace

int main() {
    testFlatSonyFrameIsLevel();
    testSmallTiltGivesSmallAngle();
    testTiltsFollowTheSensorAxes();
    testTurnedOverIsDetected();
    testOtherFramesKeepTheirFormula();
    testDegenerateInputIsFinite();
    return 0;
}
