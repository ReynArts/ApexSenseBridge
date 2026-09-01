#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace asb::dualsense {

struct DualSenseFirmwareInfo {
    std::string buildDate;
    std::string buildTime;
    std::uint16_t firmwareType = 0;
    std::uint16_t softwareSeries = 0;
    std::uint32_t hardwareInfo = 0;
    std::uint32_t firmwareVersion = 0;
    std::uint16_t updateVersion = 0;
};

[[nodiscard]] std::optional<DualSenseFirmwareInfo> decodeFirmwareFeatureReport(
    std::span<const std::uint8_t> report);

} // namespace asb::dualsense
