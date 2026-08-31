#include "flydigi/Apex5Identity.h"

#include <algorithm>
#include <array>
#include <sstream>

namespace asb::flydigi {
namespace {

constexpr std::array<std::uint8_t, 6> kApex5DeviceTypes{
    128, 129, 133, 134, 135, 136,
};

constexpr std::uint8_t checksum(std::span<const std::uint8_t> bytes) noexcept {
    std::uint8_t result = 0;
    for (const auto byte : bytes) {
        result = static_cast<std::uint8_t>(result + byte);
    }
    return result;
}

} // namespace

Apex5Identity::Apex5Identity(std::uint8_t deviceType,
                             std::uint8_t connectionType,
                             std::uint8_t batteryLevel,
                             bool charging) noexcept
    : deviceType_(deviceType),
      connectionType_(connectionType),
      batteryLevel_(batteryLevel),
      charging_(charging) {}

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
    return Apex5Identity(report[6], report[7], level, charging);
}

bool Apex5Identity::isApex5DeviceType(std::uint8_t deviceType) noexcept {
    return std::find(kApex5DeviceTypes.begin(), kApex5DeviceTypes.end(), deviceType) !=
           kApex5DeviceTypes.end();
}

std::string Apex5Identity::describe() const {
    std::ostringstream output;
    if (isApex5()) {
        output << "Apex 5 (k5, DeviceType " << static_cast<unsigned int>(deviceType_) << ')';
    } else {
        output << "unsupported Flydigi controller (DeviceType "
               << static_cast<unsigned int>(deviceType_) << ')';
    }
    return output.str();
}

} // namespace asb::flydigi
