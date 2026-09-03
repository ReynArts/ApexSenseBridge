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
constexpr std::uint8_t kApex4CmdRumble = 0x0F;
constexpr std::uint8_t kApex4CmdSetForceTriggerDInput = 0xA0;
constexpr std::uint8_t kApex4ForceTriggerEffectFamily = 0x01;
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
    const TriggerEffect& effect, bool apply = true);
[[nodiscard]] Apex4ForceTriggerReport buildApex4ForceTriggerRaw(
    const ForceTriggerCommand& command, bool apply = true);
[[nodiscard]] Apex4ForceTriggerReport buildApex4Normal(TriggerSide side);
[[nodiscard]] Apex4RumbleReport buildApex4Rumble(
    std::uint8_t lowFrequencyMotor,
    std::uint8_t highFrequencyMotor);

} // namespace asb::flydigi
