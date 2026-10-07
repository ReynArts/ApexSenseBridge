#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace asb::flydigi::apex6 {

inline constexpr std::uint16_t kVendorId = 0x37D7;
inline constexpr std::uint16_t kProductId = 0x2502;
inline constexpr std::uint16_t kUsagePage = 0xFFA0;
inline constexpr std::size_t kFrameSize = 32;
inline constexpr std::size_t kReportSize = 33;
inline constexpr std::uint8_t kReportId = 0x00;
inline constexpr std::uint8_t kMagic0 = 0x5A;
inline constexpr std::uint8_t kMagic1 = 0xA5;
inline constexpr std::uint8_t kCmdGetInfo = 0x01;
inline constexpr std::uint8_t kCmdMotorRoute = 0x53;
inline constexpr std::uint8_t kCmdRealtimeMotor = 0x57;
inline constexpr std::uint8_t kCmdOperatorData = 0xEF;
inline constexpr std::uint8_t kDeviceType = 0x96;
inline constexpr std::uint8_t kPhantomBladeZeroDeviceType = 0x98;
// Official Space Station k6 model IDs: 0x95 is the non-Pro APEX 6.
[[nodiscard]] constexpr bool isProDeviceType(std::uint8_t deviceType) noexcept {
    return deviceType == kDeviceType || deviceType == kPhantomBladeZeroDeviceType;
}
inline constexpr std::uint8_t kFeatureHaptic = 0x80;
inline constexpr std::uint8_t kFeatureTriggerHaptic = 0x10;

using Report = std::array<std::uint8_t, kReportSize>;

enum class TriggerRoute : std::uint8_t {
    Left = 0,
    Right = 1,
    Both = 2,
    Mute = 3,
};

struct MotorSubframe {
    std::int8_t trigger = 0;
    std::int8_t leftGrip = 0;
    std::int8_t rightGrip = 0;

    bool operator==(const MotorSubframe&) const = default;
};

using MotorBlock = std::array<MotorSubframe, 8>;

struct DeviceInfo {
    std::uint8_t deviceType = 0;
    std::uint8_t connectionMode = 0;
    std::uint8_t features = 0;

    [[nodiscard]] bool hasGripHaptics() const noexcept {
        return (features & kFeatureHaptic) != 0;
    }
    [[nodiscard]] bool hasTriggerHaptics() const noexcept {
        return (features & kFeatureTriggerHaptic) != 0;
    }
};

[[nodiscard]] bool isProduct(std::uint16_t vendorId,
                             std::uint16_t productId) noexcept;
[[nodiscard]] std::uint8_t checksum(
    std::span<const std::uint8_t> protocolFrame) noexcept;
[[nodiscard]] Report buildCommand(std::uint8_t command,
                                  std::span<const std::uint8_t> payload = {});
[[nodiscard]] Report buildGetInfo();
[[nodiscard]] std::array<Report, 2> buildMotorEnable();
[[nodiscard]] std::array<Report, 2> buildMotorDisable();
[[nodiscard]] Report buildRealtimeMotor(const MotorBlock& block,
                                        TriggerRoute route,
                                        bool enableTrigger = true,
                                        bool enableLeftGrip = true,
                                        bool enableRightGrip = true);
[[nodiscard]] std::optional<DeviceInfo> parseDeviceInfo(
    std::span<const std::uint8_t> report) noexcept;
[[nodiscard]] bool isValidFrame(std::span<const std::uint8_t> report,
                                std::uint8_t command) noexcept;

} // namespace asb::flydigi::apex6
