#include "flydigi/Apex5Identity.h"

#include "flydigi/Apex4Protocol.h"

#include <algorithm>
#include <array>
#include <sstream>

namespace asb::flydigi {
namespace {

constexpr std::array<std::uint8_t, 6> kApex5DeviceTypes{
    128, 129, 133, 134, 135, 136,
};

// All known k2/Apex 4 variants from Flydigi's model table. SDL currently
// identifies the common retail model as 84; the remaining IDs are regional
// and themed variants of the same force-trigger hardware.
constexpr std::array<std::uint8_t, 8> kApex4DeviceTypes{
    84, 86, 87, 92, 93, 102, 103, 104,
};

constexpr std::uint8_t checksum(std::span<const std::uint8_t> bytes) noexcept {
    std::uint8_t result = 0;
    for (const auto byte : bytes) {
        result = static_cast<std::uint8_t>(result + byte);
    }
    return result;
}

} // namespace

Apex5Identity::Apex5Identity(ApexProtocol protocol,
                             std::uint8_t deviceType,
                             std::uint8_t connectionType,
                             std::uint8_t batteryLevel,
                             bool charging,
                             std::uint16_t firmwareVersion) noexcept
    : protocol_(protocol),
      deviceType_(deviceType),
      connectionType_(connectionType),
      batteryLevel_(batteryLevel),
      charging_(charging),
      firmwareVersion_(firmwareVersion) {}

Report Apex5Identity::buildRequest() {
    Report report{};
    report[0] = kReportIdOut;
    report[1] = kMagic0;
    report[2] = kMagic1;
    report[3] = kCmdGetInfo;
    report[4] = 2; // Command byte + length byte, as expected by checksummed commands.
    report[5] = checksum(std::span<const std::uint8_t>(report).subspan(3, report[4]));
    return report;
}

std::optional<Apex5Identity> Apex5Identity::parseReply(
    std::span<const std::uint8_t> report) {
    if (report.size() < 14 ||
        report[0] != kReportIdIn ||
        report[1] != kMagic0 ||
        report[2] != kMagic1 ||
        report[3] != kCmdGetInfo) {
        return std::nullopt;
    }

    const auto rawBattery = report[12];
    const bool charging = (rawBattery >> 4U) == 1;
    const auto level = static_cast<std::uint8_t>(
        charging ? 6 : std::min<std::uint8_t>(rawBattery & 0x0F, 6));
    return Apex5Identity(ApexProtocol::CurrentV2, report[6], report[7],
                         level, charging, 0);
}

std::optional<Apex5Identity> Apex5Identity::parseApex4Reply(
    std::span<const std::uint8_t> report) {
    // Flydigi V1 replies are 32 bytes and echo GetInfo at byte 15. The device
    // ID, little-endian firmware and connection mode match SDL's V1 decoder.
    if (report.size() != 32 || report[15] != kApex4CmdGetInfo) {
        return std::nullopt;
    }
    const auto firmware = static_cast<std::uint16_t>(
        report[9] | static_cast<std::uint16_t>(report[10]) << 8U);
    return Apex5Identity(ApexProtocol::LegacyV1, report[3], report[13],
                         0, false, firmware);
}

bool Apex5Identity::isApex4DeviceType(std::uint8_t deviceType) noexcept {
    return std::find(kApex4DeviceTypes.begin(), kApex4DeviceTypes.end(), deviceType) !=
           kApex4DeviceTypes.end();
}

bool Apex5Identity::isApex5DeviceType(std::uint8_t deviceType) noexcept {
    return std::find(kApex5DeviceTypes.begin(), kApex5DeviceTypes.end(), deviceType) !=
           kApex5DeviceTypes.end();
}

std::string Apex5Identity::describe() const {
    std::ostringstream output;
    if (isApex4()) {
        output << "Apex 4 (k2, DeviceType " << static_cast<unsigned int>(deviceType_);
        if (firmwareVersion_ != 0) {
            output << ", firmware 0x" << std::hex << std::uppercase
                   << firmwareVersion_ << std::dec;
        }
        output << ')';
    } else if (isApex5()) {
        output << "Apex 5 (k5, DeviceType " << static_cast<unsigned int>(deviceType_) << ')';
    } else {
        output << "unsupported Flydigi controller (DeviceType "
               << static_cast<unsigned int>(deviceType_) << ')';
    }
    return output.str();
}

} // namespace asb::flydigi
