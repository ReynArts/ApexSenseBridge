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

    // The four rear paddles live in report[7] - M1 0x04, M2 0x08, M3 0x10,
    // M4 0x20 - and nothing else reads that byte. They are the only buttons on
    // this pad that XInput cannot express, which is why Create is applied here
    // directly rather than through mapXInputButtons, the same way the PS button
    // above is.
    //
    // Create needs its own source because View/Back is already spent: XInput has
    // no touchpad, so Back carries the touchpad click that native PlayStation
    // titles use as Map. Before this, nothing in the bridge ever set Create, and
    // a game that wanted it saw a touchpad click instead.
    //
    // Confirmed on hardware by pressing each paddle in turn: they set only their
    // own bit, so they are not mapped to duplicate another button on the pad.
    // M2 to M4 stay unmapped deliberately - their bits are written down here so
    // binding one later is a one-line change.
    if (report[7] & 0x04) state.buttons |= dualsense::button::kCreate;
    return state;
}

} // namespace asb::flydigi
