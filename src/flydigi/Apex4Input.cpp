#include "flydigi/Apex4Input.h"

#include "flydigi/Apex4Protocol.h"
#include "platform/XInputMapping.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace asb::flydigi {
namespace {

std::uint8_t normalizeStick(std::uint8_t value) noexcept {
    // Flydigi V1 rests at 0x7F while DualSense rests at 0x80. Preserve both
    // endpoints and shift only the exact centre to avoid artificial drift.
    return value == 0x7F ? 0x80 : value;
}

std::int16_t signedLittleEndian(std::span<const std::uint8_t> report,
                                std::size_t offset) noexcept {
    const auto raw = static_cast<std::uint16_t>(
        report[offset] |
        static_cast<std::uint16_t>(report[offset + 1]) << 8U);
    return static_cast<std::int16_t>(raw);
}

std::int16_t scaleSigned(std::int32_t value,
                         std::int32_t numerator,
                         std::int32_t denominator = 1) noexcept {
    const auto scaled = value * numerator / denominator;
    return static_cast<std::int16_t>((std::clamp)(
        scaled,
        static_cast<std::int32_t>((std::numeric_limits<std::int16_t>::min)()),
        static_cast<std::int32_t>((std::numeric_limits<std::int16_t>::max)())));
}

void decodeMotion(std::span<const std::uint8_t> report,
                  dualsense::DualSenseInputState& state) noexcept {
    // The 2026-09-30 wired/dongle captures expose signed 16-bit channels:
    // pitch at 26/27, yaw at NON-CONTIGUOUS bytes 18/20, roll at 29/30.
    // Bytes 4..6 are firmware mouse channels; interpreting them as raw yaw
    // introduces discontinuities around +/-256. Never consume adjacent stick
    // bytes 17 or 19 as part of the yaw sensor value.
    const auto yawRaw = static_cast<std::uint16_t>(
        report[18] | static_cast<std::uint16_t>(report[20]) << 8U);
    const auto yaw = static_cast<std::int16_t>(yawRaw);
    const auto pitch = signedLittleEndian(report, 26);
    const auto roll = signedLittleEndian(report, 29);

    // Experimental gains estimated by fitting the changing gravity vector to
    // these captures: ~0.45, ~0.25 and ~0.12 degrees/s per pitch/yaw/roll count.
    // Express these in the existing ASB motion scale (20 units per degree/s).
    // These are NOT factory calibration; yaw is the least constrained axis.
    // Captured validation traces document sensor orientation and remaining limitations.
    state.gyroX = scaleSigned(pitch, 9);
    state.gyroY = scaleSigned(yaw, 5);
    state.gyroZ = scaleSigned(-static_cast<std::int32_t>(roll), 12, 5);

    // Captured face-up gravity is ~800 on the physical Z channel. Rotate the
    // sensor frame into Sony coordinates: (-physical X, physical Z, physical Y).
    // In particular, gravity belongs on Sony Y, not Sony Z. Disabled gyro
    // mapping leaves all sensor bytes zero, which remains a neutral IMU here.
    state.accelX = scaleSigned(
        -static_cast<std::int32_t>(signedLittleEndian(report, 11)), 25, 2);
    state.accelY = scaleSigned(signedLittleEndian(report, 15), 25, 2);
    state.accelZ = scaleSigned(signedLittleEndian(report, 13), 25, 2);
}

} // namespace

void tuneApex4Gyroscope(dualsense::DualSenseInputState& state,
                        unsigned int strengthPercent,
                        unsigned int yawStrengthPercent) noexcept {
    const auto scaled = [](std::int16_t value, std::uint64_t numerator) {
        const auto result = static_cast<std::int64_t>(value) *
                            static_cast<std::int64_t>(numerator) / 100;
        return static_cast<std::int16_t>((std::clamp)(
            result,
            static_cast<std::int64_t>((std::numeric_limits<std::int16_t>::min)()),
            static_cast<std::int64_t>((std::numeric_limits<std::int16_t>::max)())));
    };
    state.gyroX = scaled(state.gyroX, strengthPercent);
    state.gyroY = scaled(
        state.gyroY,
        static_cast<std::uint64_t>(strengthPercent) * yawStrengthPercent / 100);
    state.gyroZ = scaled(state.gyroZ, strengthPercent);
}

std::optional<dualsense::DualSenseInputState>
decodeApex4InputReport(std::span<const std::uint8_t> report,
                       std::uint8_t batteryPercent,
                       std::uint8_t chargeState) noexcept {
    if (report.size() < 32 || report[0] != kApex4InputReportId ||
        report[1] != kApex4StateMarker) {
        return std::nullopt;
    }

    dualsense::DualSenseInputState state{};
    state.batteryPercent = batteryPercent;
    state.chargeState = chargeState;
    state.lx = normalizeStick(report[17]);
    state.ly = normalizeStick(report[19]);
    state.rx = normalizeStick(report[21]);
    state.ry = normalizeStick(report[22]);
    state.l2 = report[23];
    state.r2 = report[24];

    std::uint16_t buttons = 0;
    const auto primary = report[9];
    const auto secondary = report[10];
    if (primary & 0x01) buttons |= platform::xinputButton::kDpadUp;
    if (primary & 0x02) buttons |= platform::xinputButton::kDpadRight;
    if (primary & 0x04) buttons |= platform::xinputButton::kDpadDown;
    if (primary & 0x08) buttons |= platform::xinputButton::kDpadLeft;
    if (primary & 0x10) buttons |= platform::xinputButton::kA;
    if (primary & 0x20) buttons |= platform::xinputButton::kB;
    if (primary & 0x40) buttons |= platform::xinputButton::kBack;
    if (primary & 0x80) buttons |= platform::xinputButton::kX;
    if (secondary & 0x01) buttons |= platform::xinputButton::kY;
    if (secondary & 0x02) buttons |= platform::xinputButton::kStart;
    if (secondary & 0x04) buttons |= platform::xinputButton::kLeftShoulder;
    if (secondary & 0x08) buttons |= platform::xinputButton::kRightShoulder;
    if (secondary & 0x40) buttons |= platform::xinputButton::kLeftThumb;
    if (secondary & 0x80) buttons |= platform::xinputButton::kRightThumb;

    platform::mapXInputButtons(buttons, state.l2, state.r2, state);
    if (report[8] & 0x08) state.buttons |= dualsense::button::kPs;
    decodeMotion(report, state);
    return state;
}

} // namespace asb::flydigi
