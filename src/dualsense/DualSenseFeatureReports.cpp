#include "dualsense/DualSenseFeatureReports.h"

#include <array>
#include <cstddef>

namespace asb::dualsense {
namespace {

// Gyro biases are zero and the plus/minus pairs are symmetric, which makes the
// host-side calibration divisors non-zero without pretending to describe a
// physical sensor. Ported from buildCalibrationFeatureReport() in
// third_party/viiper-patches/viiper-v0.6.1-dualsense.patch.
constexpr std::array<std::uint8_t, kCalibrationFeatureReportSize> kCalibration{{
    kFeatureReportIdCalibration,
    0x00, 0x00,                   // gyro pitch bias 0
    0x00, 0x00,                   // gyro yaw bias 0
    0x00, 0x00,                   // gyro roll bias 0
    0x00, 0x04, 0x00, 0xFC,       // gyro pitch plus 1024 / minus -1024
    0x00, 0x04, 0x00, 0xFC,       // gyro yaw
    0x00, 0x04, 0x00, 0xFC,       // gyro roll
    0x40, 0x00, 0x40, 0x00,       // gyro speed plus/minus 64
    0x00, 0x20, 0x00, 0xE0,       // accel x plus 8192 / minus -8192
    0x00, 0x20, 0x00, 0xE0,       // accel y
    0x00, 0x20, 0x00, 0xE0,       // accel z
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
}};

// Report 0x09 carries the controller MAC followed by the 08 25 00 marker and
// the paired host address. Ported from buildSerialFeatureReport().
constexpr std::array<std::uint8_t, kPairingInfoFeatureReportSize> kPairingInfo{{
    kFeatureReportIdPairingInfo,
    0xC0, 0x13, 0x37, 0x05, 0x02, 0x06,
    0x08, 0x25, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
}};

// Verbatim report 0x20 captured from a physical wired DualSense running
// firmware 0x0630. Synthesizing only the update-version word left the older
// A-0356 main-firmware identity visible to games, so the whole capture is kept.
constexpr std::array<std::uint8_t, kFirmwareFeatureReportSize> kFirmware{{
    0x20, 0x4A, 0x75, 0x6C, 0x20, 0x20, 0x34, 0x20,
    0x32, 0x30, 0x32, 0x35, 0x31, 0x30, 0x3A, 0x31,
    0x30, 0x3A, 0x33, 0x32, 0x03, 0x00, 0x04, 0x00,
    0x10, 0x13, 0x00, 0x00, 0x2A, 0x00, 0x10, 0x01,
    0x01, 0xC8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x30, 0x06, 0x00, 0x00,
    0x3C, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x02, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
}};

static_assert(kCalibration.size() == kCalibrationFeatureReportSize);
static_assert(kPairingInfo.size() == kPairingInfoFeatureReportSize);
static_assert(kFirmware.size() == kFirmwareFeatureReportSize);

} // namespace

std::span<const std::uint8_t> featureReport(std::uint8_t reportId) noexcept {
    switch (reportId) {
    case kFeatureReportIdCalibration:
        return {kCalibration.data(), kCalibration.size()};
    case kFeatureReportIdPairingInfo:
        return {kPairingInfo.data(), kPairingInfo.size()};
    case kFeatureReportIdFirmware:
        return {kFirmware.data(), kFirmware.size()};
    default:
        return {};
    }
}

} // namespace asb::dualsense
