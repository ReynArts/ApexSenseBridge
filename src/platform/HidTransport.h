#pragma once

#include "core/DeviceInfo.h"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <span>
#include <string>
#include <vector>

namespace asb::platform {

enum class HidReadStatus {
    Data,
    Timeout,
    Error,
};

class HidTransport {
public:
    virtual ~HidTransport() = default;

    [[nodiscard]] virtual bool isOpen() const noexcept = 0;
    [[nodiscard]] virtual const HidDeviceInfo& info() const noexcept = 0;
    virtual bool writeOutputReport(std::span<const std::uint8_t> report, std::string& error) = 0;
    virtual bool readFeatureReport(std::span<std::uint8_t> report, std::string& error) {
        (void)report;
        error = "Reading HID feature reports is not supported by this transport.";
        return false;
    }
    virtual HidReadStatus readInputReport(
        std::span<std::uint8_t> report,
        std::chrono::milliseconds timeout,
        std::size_t& bytesRead,
        std::string& error) = 0;
};

[[nodiscard]] std::vector<HidDeviceInfo> enumerateHidDevices(std::string& error);
[[nodiscard]] HidTransport* createHidTransport(const HidDeviceInfo& info, std::string& error);
void destroyHidTransport(HidTransport* transport) noexcept;

} // namespace asb::platform
