#pragma once

#include <cstdint>
#include <span>

namespace asb::dualsense {

// Feature reports a DualSense host reads during enumeration. hid-playstation
// refuses to probe without all three, and 054C:0CE6 has no hid-generic
// fallback, so a virtual controller that cannot answer them is invisible.
inline constexpr std::uint8_t kFeatureReportIdCalibration = 0x05;
inline constexpr std::uint8_t kFeatureReportIdPairingInfo = 0x09;
inline constexpr std::uint8_t kFeatureReportIdFirmware = 0x20;

inline constexpr std::size_t kCalibrationFeatureReportSize = 41;
inline constexpr std::size_t kPairingInfoFeatureReportSize = 20;
inline constexpr std::size_t kFirmwareFeatureReportSize = 64;

// Returns the stored payload for reportId, including its leading report-ID
// byte, or an empty span when the report is not served.
[[nodiscard]] std::span<const std::uint8_t> featureReport(std::uint8_t reportId) noexcept;

} // namespace asb::dualsense
