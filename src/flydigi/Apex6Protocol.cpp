#include "flydigi/Apex6Protocol.h"

#include <algorithm>

namespace asb::flydigi::apex6 {
namespace {

std::span<const std::uint8_t> protocolFrame(
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

std::uint8_t encode(std::int8_t sample) noexcept {
    // GPA6 uses unsigned bipolar samples with 0x80 as the electrical zero.
    return static_cast<std::uint8_t>(
        static_cast<std::int16_t>(sample) + 128);
}

} // namespace

bool isProduct(std::uint16_t vendorId, std::uint16_t productId) noexcept {
    return vendorId == kVendorId && productId == kProductId;
}

std::uint8_t checksum(std::span<const std::uint8_t> frame) noexcept {
    if (frame.size() < kFrameSize) return 0;
    std::uint8_t result = 0;
    for (std::size_t index = 2; index < kFrameSize - 1; ++index) {
        result = static_cast<std::uint8_t>(result + frame[index]);
    }
    return result;
}

Report buildCommand(std::uint8_t command,
                    std::span<const std::uint8_t> payload) {
    Report report{};
    report[0] = kReportId;
    report[1] = kMagic0;
    report[2] = kMagic1;
    report[3] = command;
    const auto payloadSize = (std::min)(payload.size(), std::size_t{27});
    report[4] = static_cast<std::uint8_t>(payloadSize + 2);
    std::copy_n(payload.begin(), payloadSize, report.begin() + 5);
    report[32] = checksum(std::span<const std::uint8_t>(report).subspan(1));
    return report;
}

Report buildGetInfo() {
    return buildCommand(kCmdGetInfo);
}

Report buildProfileStatusRequest() {
    return buildCommand(kCmdProfileStatus);
}

std::optional<Report> buildApplyProfile(std::uint8_t slot) {
    if (slot >= kProfileSlotCount) return std::nullopt;
    const std::array<std::uint8_t, 1> payload{slot};
    return buildCommand(kCmdApplyProfile, payload);
}

std::optional<ProfileState> parseProfileState(
    std::span<const std::uint8_t> report, std::uint8_t command) noexcept {
    if (command != kCmdProfileStatus && command != kCmdApplyProfile) {
        return std::nullopt;
    }
    const auto frame = protocolFrame(report);
    if (frame.size() != kFrameSize || frame[2] != command ||
        frame[31] != checksum(frame)) {
        return std::nullopt;
    }
    constexpr std::size_t kPackageCountOffset = 3;
    constexpr std::size_t kPayloadOffset = 5;
    if (frame[kPackageCountOffset] == 0) return std::nullopt;

    ProfileState state{};
    state.rawSlot = frame[kPayloadOffset];
    if (state.rawSlot >= kProfileSlotCount * 2) return std::nullopt;
    const auto littleEndian = [&frame](std::size_t offset) noexcept {
        return static_cast<std::uint16_t>(
            frame[offset] | (static_cast<std::uint16_t>(frame[offset + 1]) << 8U));
    };
    for (std::size_t index = 0; index < state.slotChecksums.size(); ++index) {
        state.slotChecksums[index] = littleEndian(kPayloadOffset + 1 + index * 2);
    }
    state.globalChecksum = littleEndian(
        kPayloadOffset + 1 + state.slotChecksums.size() * 2);
    return state;
}

std::array<Report, 2> buildMotorEnable(bool realtimeGrips) {
    constexpr std::array<std::uint8_t, 5> triggers{0x01, 0x02, 0x03, 0x00, 0x40};
    constexpr std::array<std::uint8_t, 5> gripsRealtime{0x01, 0x12, 0x02, 0x40, 0x00};
    constexpr std::array<std::uint8_t, 3> gripsNative{0x01, 0x12, 0x00};
    return {buildCommand(kCmdMotorRoute, triggers),
            realtimeGrips ? buildCommand(kCmdMotorRoute, gripsRealtime)
                          : buildCommand(kCmdMotorRoute, gripsNative)};
}

std::array<Report, 2> buildTriggerSideEnable(TriggerRoute side) {
    const std::uint8_t target = side == TriggerRoute::Right ? 0x01 : 0x00;
    const std::uint8_t other = target == 0x01 ? 0x00 : 0x01;
    const std::array<std::uint8_t, 5> enable{0x01, target, 0x03, 0x00, 0x40};
    const std::array<std::uint8_t, 3> disable{0x01, other, 0x00};
    return {buildCommand(kCmdMotorRoute, enable), buildCommand(kCmdMotorRoute, disable)};
}

namespace {

std::uint8_t triggerTarget(TriggerRoute side) noexcept {
    if (side == TriggerRoute::Left) return 0x00;
    if (side == TriggerRoute::Right) return 0x01;
    return 0x02;
}

} // namespace

Report buildTriggerEnable(TriggerRoute side) {
    const std::array<std::uint8_t, 5> enable{0x01, triggerTarget(side), 0x03, 0x00, 0x40};
    return buildCommand(kCmdMotorRoute, enable);
}

Report buildTriggerDisable(TriggerRoute side) {
    const std::array<std::uint8_t, 3> disable{0x01, triggerTarget(side), 0x00};
    return buildCommand(kCmdMotorRoute, disable);
}

std::optional<Report> buildTriggerFirmwareEffect(TriggerRoute side,
                                                 const TriggerFirmwareEffect& effect) {
    if ((side != TriggerRoute::Left && side != TriggerRoute::Right) ||
        effect.type > kTriggerFirmwareOneShot) {
        return std::nullopt;
    }
    const std::array<std::uint8_t, 7> payload{
        0x01, triggerTarget(side), 0x04, effect.type, effect.strength, effect.parameter,
        effect.start};
    return buildCommand(kCmdMotorRoute, payload);
}

std::array<Report, 2> buildMotorDisable() {
    constexpr std::array<std::uint8_t, 3> first{0x01, 0x02, 0x00};
    constexpr std::array<std::uint8_t, 3> second{0x01, 0x12, 0x00};
    return {buildCommand(kCmdMotorRoute, first),
            buildCommand(kCmdMotorRoute, second)};
}

Report buildRealtimeMotor(const MotorBlock& block,
                          TriggerRoute route,
                          bool enableTrigger,
                          bool enableLeftGrip,
                          bool enableRightGrip,
                          bool interpolate) {
    std::array<std::uint8_t, 25> payload{};
    payload[0] = static_cast<std::uint8_t>(
        (interpolate ? 0x80U : 0U) |
        (enableRightGrip ? 0x10U : 0U) |
        (enableLeftGrip ? 0x08U : 0U) |
        (enableTrigger ? 0x04U : 0U) |
        static_cast<std::uint8_t>(route));
    for (std::size_t index = 0; index < block.size(); ++index) {
        const auto offset = 1 + index * 3;
        payload[offset] = enableTrigger ? encode(block[index].trigger) : 0x80;
        payload[offset + 1] = enableLeftGrip ? encode(block[index].leftGrip) : 0x80;
        payload[offset + 2] = enableRightGrip ? encode(block[index].rightGrip) : 0x80;
    }
    return buildCommand(kCmdRealtimeMotor, payload);
}

bool isValidFrame(std::span<const std::uint8_t> report,
                  std::uint8_t command) noexcept {
    const auto frame = protocolFrame(report);
    return frame.size() == kFrameSize && frame[2] == command &&
           frame[31] == checksum(frame);
}

std::optional<DeviceInfo> parseDeviceInfo(
    std::span<const std::uint8_t> report) noexcept {
    const auto frame = protocolFrame(report);
    if (frame.size() != kFrameSize || frame[2] != kCmdGetInfo ||
        frame[31] != checksum(frame)) {
        return std::nullopt;
    }

    // GPA6 command responses use the v1 input layout:
    // magic, command, package count, package index, then payload.
    constexpr std::size_t kPayloadOffset = 5;
    constexpr std::size_t kBatteryOffset = kPayloadOffset + 6;
    constexpr std::size_t kFirmwareOffset = kPayloadOffset + 10;
    constexpr std::size_t kFeatureOffset = kPayloadOffset + 24;
    if (kFeatureOffset >= frame.size() - 1) return std::nullopt;

    const auto bigEndian = [&frame](std::size_t offset) noexcept {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(frame[offset]) << 8U) | frame[offset + 1]);
    };
    DeviceInfo info{frame[kPayloadOffset], frame[kPayloadOffset + 1],
                    frame[kFeatureOffset]};
    info.batteryLevel = static_cast<std::uint8_t>(frame[kBatteryOffset] & 0x0F);
    info.chargingStatus = static_cast<std::uint8_t>((frame[kBatteryOffset] >> 4U) & 0x0F);
    info.firmware = FirmwareVersions{
        bigEndian(kFirmwareOffset),      bigEndian(kFirmwareOffset + 2),
        bigEndian(kFirmwareOffset + 4),  bigEndian(kFirmwareOffset + 6),
        bigEndian(kFirmwareOffset + 8),  bigEndian(kFirmwareOffset + 10),
        bigEndian(kFirmwareOffset + 12)};
    return info;
}

} // namespace asb::flydigi::apex6
