#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace asb {

struct HidDeviceInfo {
    std::wstring path;
    std::wstring manufacturer;
    std::wstring product;
    std::wstring serial;
    std::wstring friendlyName;
    std::wstring className;
    std::wstring instanceId;
    std::wstring parentInstanceId;
    std::wstring interfaceNumber;
    std::vector<std::wstring> hardwareIds;
    std::vector<std::wstring> compatibleIds;
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    std::uint16_t usagePage = 0;
    std::uint16_t usage = 0;
    std::uint16_t inputReportLength = 0;
    std::uint16_t outputReportLength = 0;
    std::uint16_t featureReportLength = 0;
};

} // namespace asb
