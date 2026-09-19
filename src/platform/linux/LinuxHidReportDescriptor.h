#pragma once

#include <cstdint>
#include <span>

namespace asb::platform {

// The subset of HIDP_CAPS the bridge relies on, recovered from a raw report
// descriptor. Report lengths follow the Windows convention and INCLUDE the
// leading report-ID byte whenever the descriptor declares numbered reports:
// candidate selection (featureReportLength >= 46) and every transport write
// are calibrated against that convention.
struct HidReportDescriptorInfo {
    std::uint16_t usagePage = 0;
    std::uint16_t usage = 0;
    std::uint16_t inputReportLength = 0;
    std::uint16_t outputReportLength = 0;
    std::uint16_t featureReportLength = 0;
};

// Parses descriptor bytes as read from hidraw. Returns false only for a
// malformed descriptor; a well-formed descriptor with no reports of a given
// kind simply leaves that length at zero.
[[nodiscard]] bool parseHidReportDescriptor(std::span<const std::uint8_t> descriptor,
                                            HidReportDescriptorInfo& info) noexcept;

} // namespace asb::platform
