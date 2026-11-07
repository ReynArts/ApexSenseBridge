#pragma once

#include "flydigi/Apex6Protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace asb::flydigi::apex6 {

inline constexpr std::uint8_t kFeatureImu = 0x01;
inline constexpr std::uint8_t kCmdReadTransport = 0x10;
inline constexpr std::uint8_t kCmdSetTransport = 0x11;
inline constexpr std::uint8_t kTransportKeep = 0xFF;
inline constexpr std::size_t kOperatorGyroOffset = 17;
inline constexpr std::size_t kOperatorAccelOffset = 23;
inline constexpr std::size_t kOperatorMinimumFrame = 29;

struct RawMotion {
    std::array<std::int16_t, 3> gyro{};
    std::array<std::int16_t, 3> accel{};

    bool operator==(const RawMotion&) const = default;
};

struct MotionSample {
    std::int16_t gyroX = 0;
    std::int16_t gyroY = 0;
    std::int16_t gyroZ = 0;
    std::int16_t accelX = 0;
    std::int16_t accelY = 0;
    std::int16_t accelZ = 0;

    bool operator==(const MotionSample&) const = default;
};

struct MotionCalibration {
    std::array<std::uint8_t, 3> gyroSource{1, 0, 2};
    std::array<std::int8_t, 3> gyroSign{1, -1, -1};
    std::array<std::uint8_t, 3> accelSource{0, 2, 1};
    std::array<std::int8_t, 3> accelSign{1, 1, -1};
    std::int32_t gyroNumerator = 1;
    std::int32_t gyroDenominator = 1;
    std::int32_t accelNumerator = 1051;
    std::int32_t accelDenominator = 500;
};

inline constexpr MotionCalibration kDefaultMotionCalibration{};

enum class OperatorParse {
    NotOperatorFrame,
    InvalidChecksum,
    Motion,
};

struct TransportStatus {
    std::uint8_t field0 = 0;
    std::uint8_t field1 = 0;
    std::uint8_t field2 = 0;
    std::uint8_t field3 = 0;
    std::uint8_t field4 = 0;

    [[nodiscard]] bool takeoverActive() const noexcept { return field0 != 0; }
    [[nodiscard]] bool allowThirdParty() const noexcept { return field4 != 0; }

    bool operator==(const TransportStatus&) const = default;
};

[[nodiscard]] constexpr bool hasImu(std::uint8_t features) noexcept {
    return (features & kFeatureImu) != 0;
}

[[nodiscard]] OperatorParse parseOperatorReport(
    std::span<const std::uint8_t> report, RawMotion& motion) noexcept;

[[nodiscard]] MotionSample convertMotion(
    const RawMotion& raw,
    const MotionCalibration& calibration = kDefaultMotionCalibration) noexcept;

[[nodiscard]] Report buildReadTransport();
[[nodiscard]] Report buildSetRawDataRouting(bool enable);
[[nodiscard]] std::optional<TransportStatus> parseTransportStatus(
    std::span<const std::uint8_t> report) noexcept;
[[nodiscard]] std::optional<std::uint8_t> parseGyroChipType(
    std::span<const std::uint8_t> report) noexcept;

} // namespace asb::flydigi::apex6
