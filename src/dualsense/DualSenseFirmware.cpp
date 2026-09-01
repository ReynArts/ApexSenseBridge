#include "dualsense/DualSenseFirmware.h"

#include <cstddef>

namespace asb::dualsense {
namespace {

std::uint16_t readU16(std::span<const std::uint8_t> report, std::size_t offset) {
    return static_cast<std::uint16_t>(report[offset]) |
           (static_cast<std::uint16_t>(report[offset + 1]) << 8);
}

std::uint32_t readU32(std::span<const std::uint8_t> report, std::size_t offset) {
    return static_cast<std::uint32_t>(report[offset]) |
           (static_cast<std::uint32_t>(report[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(report[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(report[offset + 3]) << 24);
}

std::string fixedString(std::span<const std::uint8_t> report,
                        std::size_t offset,
                        std::size_t length) {
    std::string result(reinterpret_cast<const char*>(report.data() + offset), length);
    while (!result.empty() && result.back() == '\0') result.pop_back();
    return result;
}

} // namespace

std::optional<DualSenseFirmwareInfo> decodeFirmwareFeatureReport(
    std::span<const std::uint8_t> report) {
    constexpr std::uint8_t kFirmwareReportId = 0x20;
    constexpr std::size_t kMinimumReportSize = 46;
    if (report.size() < kMinimumReportSize || report[0] != kFirmwareReportId) {
        return std::nullopt;
    }

    DualSenseFirmwareInfo info{};
    info.buildDate = fixedString(report, 1, 11);
    info.buildTime = fixedString(report, 12, 8);
    info.firmwareType = readU16(report, 20);
    info.softwareSeries = readU16(report, 22);
    info.hardwareInfo = readU32(report, 24);
    info.firmwareVersion = readU32(report, 28);
    info.updateVersion = readU16(report, 44);
    return info;
}

} // namespace asb::dualsense
