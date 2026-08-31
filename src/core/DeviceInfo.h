#pragma once

#include <cstdint>
#include <string>

namespace asb {

struct HidDeviceInfo {
    std::wstring path;
    std::wstring manufacturer;
    std::wstring product;
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    std::uint16_t usagePage = 0;
    std::uint16_t usage = 0;
    std::uint16_t inputReportLength = 0;
    std::uint16_t outputReportLength = 0;
};

} // namespace asb
