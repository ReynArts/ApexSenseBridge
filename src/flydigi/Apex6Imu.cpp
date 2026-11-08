#include "flydigi/Apex6Imu.h"

#include <algorithm>
#include <limits>

namespace asb::flydigi::apex6 {
namespace {

std::span<const std::uint8_t> locateFrame(
    std::span<const std::uint8_t> report) noexcept {
    if (report.size() >= kReportSize && report[0] == kReportId &&
        report[1] == kMagic0 && report[2] == kMagic1) {
        return report.subspan(1, kFrameSize);
    }
    if (report.size() >= kFrameSize && report[0] == kMagic0 &&
        report[1] == kMagic1) {
        return report.first(kFrameSize);
    }
    return {};
}

std::int16_t signedLittleEndian(std::span<const std::uint8_t> frame,
                                std::size_t offset) noexcept {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(
        frame[offset] | static_cast<std::uint16_t>(frame[offset + 1]) << 8U));
}

std::int16_t scale(std::int16_t value,
                   std::int8_t sign,
                   std::int32_t numerator,
                   std::int32_t denominator) noexcept {
    const auto scaled = static_cast<std::int64_t>(value) * sign * numerator /
                        denominator;
    return static_cast<std::int16_t>((std::clamp)(
        scaled,
        static_cast<std::int64_t>((std::numeric_limits<std::int16_t>::min)()),
        static_cast<std::int64_t>((std::numeric_limits<std::int16_t>::max)())));
}

} // namespace

OperatorParse parseOperatorReport(std::span<const std::uint8_t> report,
                                  RawMotion& motion) noexcept {
    const auto frame = locateFrame(report);
    if (frame.size() != kFrameSize || frame[2] != kCmdOperatorData) {
        return OperatorParse::NotOperatorFrame;
    }
    if (frame[31] != checksum(frame)) return OperatorParse::InvalidChecksum;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        motion.gyro[axis] = signedLittleEndian(frame, kOperatorGyroOffset + axis * 2);
        motion.accel[axis] = signedLittleEndian(frame, kOperatorAccelOffset + axis * 2);
    }
    return OperatorParse::Motion;
}

MotionSample convertMotion(const RawMotion& raw,
                           const MotionCalibration& calibration) noexcept {
    const auto gyro = [&](std::size_t axis) {
        return scale(raw.gyro[calibration.gyroSource[axis] % 3],
                     calibration.gyroSign[axis], calibration.gyroNumerator,
                     calibration.gyroDenominator);
    };
    const auto accel = [&](std::size_t axis) {
        return scale(raw.accel[calibration.accelSource[axis] % 3],
                     calibration.accelSign[axis], calibration.accelNumerator,
                     calibration.accelDenominator);
    };
    return MotionSample{gyro(0), gyro(1), gyro(2), accel(0), accel(1), accel(2)};
}

Report buildReadTransport() {
    return buildCommand(kCmdReadTransport);
}

Report buildSetRawDataRouting(bool enable) {
    std::array<std::uint8_t, 27> payload{};
    payload[0] = kTransportKeep;
    payload[1] = enable ? 1 : 0;
    payload[2] = kTransportKeep;
    payload[3] = kTransportKeep;
    payload[4] = kTransportKeep;
    return buildCommand(kCmdSetTransport, payload);
}

std::optional<TransportStatus> parseTransportStatus(
    std::span<const std::uint8_t> report) noexcept {
    const auto frame = locateFrame(report);
    if (frame.size() != kFrameSize || frame[2] != kCmdReadTransport ||
        frame[31] != checksum(frame) || frame[3] == 0) {
        return std::nullopt;
    }
    return TransportStatus{frame[5], frame[6], frame[7], frame[8], frame[9]};
}

std::optional<std::uint8_t> parseGyroChipType(
    std::span<const std::uint8_t> report) noexcept {
    const auto frame = locateFrame(report);
    if (frame.size() != kFrameSize || frame[2] != kCmdGetInfo ||
        frame[31] != checksum(frame)) {
        return std::nullopt;
    }
    constexpr std::size_t kGyroTypeOffset = 13;
    return static_cast<std::uint8_t>(frame[kGyroTypeOffset] & 0x0F);
}

} // namespace asb::flydigi::apex6
