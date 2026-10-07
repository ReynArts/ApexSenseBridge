#include "flydigi/Apex5Protocol.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>

namespace asb::flydigi {
namespace {

std::uint8_t atLeastOne(std::uint8_t value) noexcept {
    return std::max<std::uint8_t>(1, value);
}

Report buildCommand81(std::initializer_list<std::uint8_t> payload) {
    Report report{};
    report[0] = kReportIdOut;
    report[1] = kMagic0;
    report[2] = kMagic1;
    report[3] = kCmdSetForceTrigger;
    report[4] = static_cast<std::uint8_t>(payload.size());

    std::size_t offset = 5;
    for (const auto byte : payload) {
        if (offset >= report.size()) {
            break;
        }
        report[offset++] = byte;
    }
    return report;
}

Report buildChecksummedCommand(
    std::uint8_t command, std::span<const std::uint8_t> payload) {
    Report report{};
    report[0] = kReportIdOut;
    report[1] = kMagic0;
    report[2] = kMagic1;
    report[3] = command;
    report[4] = static_cast<std::uint8_t>(payload.size() + 2);
    std::copy(payload.begin(), payload.end(), report.begin() + 5);

    std::uint8_t checksum = 0;
    for (std::size_t index = 3; index < 3 + report[4]; ++index) {
        checksum = static_cast<std::uint8_t>(checksum + report[index]);
    }
    report[3 + report[4]] = checksum;
    return report;
}

} // namespace

bool isControllerProduct(std::uint16_t productId) noexcept {
    return (productId >> 12U) == kControllerProductFamily;
}

Report buildForceTrigger(const TriggerEffect& effect, bool apply) {
    const auto side = static_cast<std::uint8_t>(effect.side);
    const auto mode = static_cast<std::uint8_t>(effect.mode);
    const auto applyFlag = static_cast<std::uint8_t>(apply ? 1 : 0);

    switch (effect.mode) {
    case TriggerMode::Normal:
        return buildCommand81({applyFlag, side, mode});

    case TriggerMode::Race: {
        const auto match = static_cast<std::uint8_t>(
            (effect.start == 0 && effect.matchInput) ? 0 : (effect.matchInput ? 1 : 0));
        return buildCommand81({applyFlag, side, mode, effect.start,
                               atLeastOne(effect.p1), match});
    }

    case TriggerMode::RecoilRattle:
        return buildCommand81({applyFlag, side, mode, effect.start,
                               atLeastOne(effect.p1), atLeastOne(effect.p2),
                               atLeastOne(effect.p3),
                               static_cast<std::uint8_t>(effect.matchInput ? 1 : 0)});

    case TriggerMode::SniperBreak:
        return buildCommand81({applyFlag, side, mode, effect.start,
                               atLeastOne(effect.p1), atLeastOne(effect.p2), 0,
                               static_cast<std::uint8_t>(effect.matchInput ? 1 : 0)});

    case TriggerMode::Lock:
        return buildCommand81({applyFlag, side, mode, effect.start, 255,
                               static_cast<std::uint8_t>(effect.matchInput ? 1 : 0)});

    case TriggerMode::Vibration:
        // Live mode 5 remains under-documented. Keep packet construction explicit,
        // but the application does not expose this mode in the hardware test yet.
        return buildCommand81({applyFlag, side, mode, effect.start,
                               atLeastOne(effect.p1), atLeastOne(effect.p2),
                               atLeastOne(effect.p3),
                               static_cast<std::uint8_t>(effect.matchInput ? 1 : 0)});
    }

    return buildNormal(effect.side);
}

Report buildForceTriggerRaw(const ForceTriggerCommand& command, bool apply) {
    Report report{};
    report[0] = kReportIdOut;
    report[1] = kMagic0;
    report[2] = kMagic1;
    report[3] = kCmdSetForceTrigger;
    report[4] = 10;
    report[5] = static_cast<std::uint8_t>(apply ? 1 : 0);
    report[6] = static_cast<std::uint8_t>(command.side);
    report[7] = static_cast<std::uint8_t>(command.mode);
    std::copy(command.params.begin(), command.params.end(), report.begin() + 8);

    // Flydigi ForceTriggerConfigCommon quirk, retained byte-for-byte.
    if (command.mode == TriggerMode::Race && report[8] == 0 && report[10] == 1) {
        report[10] = 0;
    }
    return report;
}

Report buildNormal(TriggerSide side) {
    TriggerEffect effect{};
    effect.side = side;
    effect.mode = TriggerMode::Normal;
    return buildForceTrigger(effect, true);
}

Report buildRumble(std::uint8_t lowFrequencyMotor,
                   std::uint8_t highFrequencyMotor) {
    Report report{};
    report[0] = kReportIdOut;
    report[1] = kMagic0;
    report[2] = kMagic1;
    report[3] = kCmdSetRumble;
    report[4] = 6;
    report[5] = lowFrequencyMotor;
    report[6] = highFrequencyMotor;
    return report;
}

Report buildReadRgbConfig(std::uint8_t slot, std::uint8_t packetSize) {
    const std::array<std::uint8_t, 2> payload{slot, packetSize};
    return buildChecksummedCommand(kCmdReadRgbConfig, payload);
}

Report buildWriteRgbStart(std::uint8_t slot, std::uint8_t startIndex,
                          std::uint8_t packetCount, std::uint8_t packetSize) {
    const std::array<std::uint8_t, 4> payload{slot, startIndex, packetCount, packetSize};
    return buildChecksummedCommand(kCmdWriteRgbStart, payload);
}

Report buildWriteRgbPack(std::uint8_t packetIndex, std::span<const std::uint8_t> data) {
    std::array<std::uint8_t, 1 + kRgbPacketSize> payload{};
    payload[0] = packetIndex;
    const auto copyLen = std::min<std::size_t>(data.size(), kRgbPacketSize);
    std::copy_n(data.begin(), copyLen, payload.begin() + 1);
    return buildChecksummedCommand(
        kCmdWriteRgbPack,
        std::span<const std::uint8_t>(payload.data(), 1 + copyLen));
}

std::array<std::uint8_t, kRgbConfigSize> buildStaticRgbPayload(
    std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t brightness) {
    std::array<std::uint8_t, kRgbConfigSize> payload{};
    payload[0] = 0x00;
    payload[1] = 0x03;
    payload[2] = 0x00;
    payload[3] = 0x00;
    payload[4] = 0x00;
    payload[5] = 0x00;
    payload[6] = brightness;
    payload[7] = static_cast<std::uint8_t>(kApex5LedCount);
    payload[8] = 0x04;
    payload[9] = 0x00;
    std::fill_n(payload.begin() + 10, 10, std::uint8_t{0xFF});
    for (std::size_t offset = 20; offset + 2 < kRgbConfigSize; offset += 3) {
        payload[offset] = r;
        payload[offset + 1] = g;
        payload[offset + 2] = b;
    }
    return payload;
}

Report buildProfileStatusRequest() {
    return buildChecksummedCommand(kCmdProfileStatus, {});
}

std::optional<Report> buildApplyProfile(std::uint8_t slot) {
    if (slot >= kProfileSlotCount) return std::nullopt;
    const std::array payload{slot};
    return buildChecksummedCommand(kCmdApplyProfile, payload);
}

Report buildInputTransportStatusRequest() {
    return buildChecksummedCommand(kCmdReadInputTransport, {});
}

Report buildSetInputTransport(bool controllerData, bool rawData) {
    // Keyboard, mouse and third-party ownership are deliberately left
    // unchanged. 0xFF is Flydigi's "keep current value" sentinel.
    const std::array<std::uint8_t, 5> payload{
        static_cast<std::uint8_t>(controllerData ? 1 : 0),
        static_cast<std::uint8_t>(rawData ? 1 : 0),
        0xFF, 0xFF, 0xFF,
    };
    return buildChecksummedCommand(kCmdSetInputTransport, payload);
}

bool isProfileCommandReply(
    std::span<const std::uint8_t> report, std::uint8_t command) noexcept {
    return report.size() >= 4 &&
           report[0] == kReportIdIn &&
           report[1] == kMagic0 &&
           report[2] == kMagic1 &&
           report[3] == command;
}

std::optional<ProfileStatus> parseProfileStatus(
    std::span<const std::uint8_t> report) noexcept {
    if (report.size() < 7 ||
        !isProfileCommandReply(report, kCmdProfileStatus)) {
        return std::nullopt;
    }

    const auto rawSlot = report[6];
    if (rawSlot >= kProfileSlotCount * 2) return std::nullopt;
    const bool switchBank = rawSlot >= kProfileSlotCount;
    return ProfileStatus{
        rawSlot,
        static_cast<std::uint8_t>(
            switchBank ? rawSlot - kProfileSlotCount : rawSlot),
        switchBank,
    };
}

std::optional<InputTransportStatus> parseInputTransportStatus(
    std::span<const std::uint8_t> report) noexcept {
    if (report.size() < 11 ||
        !isProfileCommandReply(report, kCmdReadInputTransport)) {
        return std::nullopt;
    }
    for (std::size_t index = 6; index <= 10; ++index) {
        if (report[index] > 1) return std::nullopt;
    }
    return InputTransportStatus{
        report[6] != 0,
        report[7] != 0,
        report[8] != 0,
        report[9] != 0,
        report[10] != 0,
    };
}

} // namespace asb::flydigi
