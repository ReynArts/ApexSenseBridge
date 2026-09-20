#pragma once

#include "core/TriggerEffect.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace asb::flydigi {

// Apex 4 uses Flydigi's first-generation vendor protocol on USB interface 2.
constexpr std::uint16_t kApex4VendorId = 0x04B4;
constexpr std::uint16_t kApex4ProductId = 0x2412;
constexpr std::uint16_t kApex4VendorUsagePage = 0xFFA0;
constexpr std::uint8_t kApex4CommandReportId = 0x05;
constexpr std::uint8_t kApex4InputReportId = 0x04;
constexpr std::uint8_t kApex4StateMarker = 0xFE;
constexpr std::uint8_t kApex4CmdGetInfo = 0xEC;

// How many identity requests a session is allowed to send. Kept small and
// spaced on purpose: the controller falls silent for roughly thirty seconds
// after a burst of vendor queries, so extra attempts cost sessions instead of
// saving them. Exposed here so the protocol tests pin the same budget.
constexpr std::size_t kApex4IdentityAttempts = 3;
constexpr std::uint8_t kApex4CmdRumble = 0x0F;
constexpr std::uint8_t kApex4CmdSetForceTriggerDInput = 0xA0;
constexpr std::uint8_t kApex4ForceTriggerEffectFamily = 0x01;

// Byte 3 of the force-trigger report is Flydigi's "apply" flag, and the name
// misleads. Clearing it does not stop the effect engaging: an Apex 4 given a
// report with the flag cleared produces exactly the same resistance, and lets
// it go again on a cleared Normal, both confirmed blind - twelve rounds each
// way, a coin deciding the content, 12/12 with no false positive or miss, and
// a control with the flag set scoring the same.
//
// What the flag does decide is whether the pad performs some further and
// expensive step. With it set, an Apex 4 on its 2.4 GHz dongle stops its
// entire main loop for a fixed ~1073 ms per command - input sampling, buttons
// and motors all halt together and resume together - while over a USB cable
// the identical command costs nothing. In game the dongle behaviour reads as
// the camera continuing to turn after the stick is released.
//
// So the bridge clears it. The effects are the same; the second-long stall is
// not. Full measurements and everything ruled out on the way:
// docs/APEX4-DONGLE-TRIGGER-STALL.md.
constexpr bool kApex4ApplyFlag = false;
constexpr std::size_t kApex4IdentityRequestSize = 12;
constexpr std::size_t kApex4ForceTriggerReportSize = 15;

using Apex4IdentityRequest =
    std::array<std::uint8_t, kApex4IdentityRequestSize>;
using Apex4ForceTriggerReport =
    std::array<std::uint8_t, kApex4ForceTriggerReportSize>;
using Apex4RumbleReport = std::array<std::uint8_t, 4>;

[[nodiscard]] bool isApex4Product(std::uint16_t vendorId,
                                  std::uint16_t productId) noexcept;
[[nodiscard]] Apex4IdentityRequest buildApex4IdentityRequest();
[[nodiscard]] Apex4ForceTriggerReport buildApex4ForceTrigger(
    const TriggerEffect& effect, bool apply = kApex4ApplyFlag);
[[nodiscard]] Apex4ForceTriggerReport buildApex4ForceTriggerRaw(
    const ForceTriggerCommand& command, bool apply = kApex4ApplyFlag);
[[nodiscard]] Apex4ForceTriggerReport buildApex4Normal(TriggerSide side);
[[nodiscard]] Apex4RumbleReport buildApex4Rumble(
    std::uint8_t lowFrequencyMotor,
    std::uint8_t highFrequencyMotor);

} // namespace asb::flydigi
