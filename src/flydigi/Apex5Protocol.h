#pragma once

#include "core/TriggerEffect.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace asb::flydigi {

// Vendor HID command interface documented/measured by OpenFlydigi.
constexpr std::uint16_t kVendorId = 0x37D7;
constexpr std::uint16_t kVendorUsagePage = 0xFFA0;
constexpr std::uint8_t kControllerProductFamily = 0x2; // PID >> 12

constexpr std::uint8_t kReportIdOut = 0x03;
constexpr std::uint8_t kMagic0 = 0x5A;
constexpr std::uint8_t kMagic1 = 0xA5;
constexpr std::uint8_t kCmdSetForceTrigger = 81;
constexpr std::size_t kReportSize = 32;

using Report = std::array<std::uint8_t, kReportSize>;

[[nodiscard]] Report buildForceTrigger(const TriggerEffect& effect, bool apply = true);
[[nodiscard]] Report buildNormal(TriggerSide side);
[[nodiscard]] bool isControllerProduct(std::uint16_t productId) noexcept;

} // namespace asb::flydigi
