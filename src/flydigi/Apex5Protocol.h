#pragma once

#include "core/TriggerEffect.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace asb::flydigi {

// Vendor HID command interface documented/measured by OpenFlydigi.
constexpr std::uint16_t kVendorId = 0x37D7;
constexpr std::uint16_t kVendorUsagePage = 0xFFA0;
constexpr std::uint8_t kControllerProductFamily = 0x2; // PID >> 12

constexpr std::uint8_t kReportIdOut = 0x03;
constexpr std::uint8_t kReportIdIn = 0x04;
constexpr std::uint8_t kMagic0 = 0x5A;
constexpr std::uint8_t kMagic1 = 0xA5;
constexpr std::uint8_t kCmdGetInfo = 0x01;
constexpr std::uint8_t kCmdReadInputTransport = 0x10;
constexpr std::uint8_t kCmdSetInputTransport = 0x11;
constexpr std::uint8_t kCmdSetRumble = 0x12;
constexpr std::uint8_t kCmdOperatorData = 0xEF;
constexpr std::uint8_t kCmdSetForceTrigger = 81;
constexpr std::uint8_t kCmdProfileStatus = 0xA1;
constexpr std::uint8_t kCmdApplyProfile = 0xA2;
constexpr std::uint8_t kCmdReadRgbConfig = 0xA7;
constexpr std::uint8_t kCmdWriteRgbStart = 0xA8;
constexpr std::uint8_t kCmdWriteRgbPack = 0xA9;
constexpr std::uint8_t kRgbPacketSize = 20;
constexpr std::uint8_t kRgbPacketCount = 19;
constexpr std::size_t kRgbConfigSize = 380;
// Header byte of the RGB configuration holding the LED brightness (0..100).
constexpr std::size_t kRgbBrightnessOffset = 6;
constexpr std::size_t kApex5LedCount = 12;
constexpr std::uint8_t kProfileSlotCount = 4;
constexpr std::size_t kReportSize = 32;

using Report = std::array<std::uint8_t, kReportSize>;

struct ProfileChecksums {
    std::array<std::uint16_t, kProfileSlotCount> slots{};
    std::uint16_t global = 0;

    bool operator==(const ProfileChecksums&) const = default;
};

struct ProfileStatus {
    std::uint8_t rawSlot = 0;
    std::uint8_t slot = 0;
    bool switchBank = false;
    std::optional<ProfileChecksums> checksums{};
};

struct TransportFlag {
    std::uint8_t raw = 0;

    [[nodiscard]] constexpr std::optional<bool> value() const noexcept {
        if (raw > 1) return std::nullopt;
        return raw == 1;
    }

    bool operator==(const TransportFlag&) const = default;
};

struct InputTransportStatus {
    bool controllerData = true;
    bool rawData = false;
    TransportFlag keyboardData{};
    TransportFlag mouseData{};
    TransportFlag thirdPartyControl{};
};

[[nodiscard]] Report buildForceTrigger(const TriggerEffect& effect, bool apply = true);
[[nodiscard]] Report buildForceTriggerRaw(const ForceTriggerCommand& command,
                                          bool apply = true);
[[nodiscard]] Report buildNormal(TriggerSide side);
[[nodiscard]] Report buildRumble(std::uint8_t lowFrequencyMotor,
                                 std::uint8_t highFrequencyMotor);
[[nodiscard]] Report buildReadRgbConfig(std::uint8_t slot = 0, std::uint8_t packetSize = kRgbPacketSize);
[[nodiscard]] Report buildWriteRgbStart(std::uint8_t slot, std::uint8_t startIndex, std::uint8_t packetCount, std::uint8_t packetSize = kRgbPacketSize);
[[nodiscard]] Report buildWriteRgbPack(std::uint8_t packetIndex, std::span<const std::uint8_t> data);
[[nodiscard]] std::array<std::uint8_t, kRgbConfigSize> buildStaticRgbPayload(
    std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t brightness = 100);
[[nodiscard]] Report buildProfileStatusRequest();
[[nodiscard]] std::optional<Report> buildApplyProfile(std::uint8_t slot);
[[nodiscard]] Report buildInputTransportStatusRequest();
[[nodiscard]] Report buildSetInputTransport(bool controllerData, bool rawData);
[[nodiscard]] bool isProfileCommandReply(
    std::span<const std::uint8_t> report, std::uint8_t command) noexcept;
[[nodiscard]] std::optional<ProfileStatus> parseProfileStatus(
    std::span<const std::uint8_t> report) noexcept;
[[nodiscard]] std::optional<InputTransportStatus> parseInputTransportStatus(
    std::span<const std::uint8_t> report) noexcept;
[[nodiscard]] bool isControllerProduct(std::uint16_t productId) noexcept;

} // namespace asb::flydigi
