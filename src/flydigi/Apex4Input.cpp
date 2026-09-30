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

std::int16_t signed12(std::uint8_t low, std::uint16_t high) noexcept {
    auto raw = static_cast<std::uint16_t>(low | high);
    if ((raw & 0x0800U) != 0) raw |= 0xF000U;
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
    // Hardware captures of the legacy 04 FE stream show two signed 12-bit
    // angular rates packed into bytes 4..6, roll as signed LE at 29, and the
    // three accelerometer axes as signed LE at 11, 13 and 15. The firmware
    // emits these fields only while the active profile's gyro mapping is not
    // Off; an inactive stream therefore decodes to a neutral IMU.
    const auto yaw = signed12(
        report[4], static_cast<std::uint16_t>(report[6] & 0xF0U) << 4U);
    const auto pitch = signed12(
        report[5], static_cast<std::uint16_t>(report[6] & 0x0FU) << 8U);
    const auto roll = signedLittleEndian(report, 29);

    // The legacy firmware's yaw and pitch channels use different gains. On
    // measured hardware pitch is approximately eight times as sensitive as
    // yaw for the same slow rotation. Normalize that asymmetry before feeding
    // the virtual DualSense calibration (20 raw units per degree/second).
    state.gyroX = scaleSigned(-static_cast<std::int32_t>(pitch), 2);
    state.gyroY = scaleSigned(-static_cast<std::int32_t>(yaw), 16);
    state.gyroZ = scaleSigned(-static_cast<std::int32_t>(roll), 1);

    // A stationary, face-up Apex 4 reports approximately +800 on Z. The
    // virtual DualSense advertises 10000 raw units per g.
    state.accelX = scaleSigned(
        -static_cast<std::int32_t>(signedLittleEndian(report, 11)), 25, 2);
    state.accelY = scaleSigned(signedLittleEndian(report, 13), 25, 2);
    state.accelZ = scaleSigned(signedLittleEndian(report, 15), 25, 2);
}

} // namespace

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
