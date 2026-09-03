#include "flydigi/Apex4Input.h"

#include "flydigi/Apex4Protocol.h"
#include "platform/XInputMapping.h"

#include <cstdint>

namespace asb::flydigi {
namespace {

std::uint8_t normalizeStick(std::uint8_t value) noexcept {
    // Flydigi V1 rests at 0x7F while DualSense rests at 0x80. Preserve both
    // endpoints and shift only the exact centre to avoid artificial drift.
    return value == 0x7F ? 0x80 : value;
}

} // namespace

std::optional<dualsense::DualSenseInputState>
decodeApex4InputReport(std::span<const std::uint8_t> report) noexcept {
    if (report.size() < 32 || report[0] != kApex4InputReportId ||
        report[1] != kApex4StateMarker) {
        return std::nullopt;
    }

    dualsense::DualSenseInputState state{};
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
    return state;
}

} // namespace asb::flydigi
