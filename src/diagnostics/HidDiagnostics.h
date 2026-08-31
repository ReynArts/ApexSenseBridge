#pragma once

#include "core/DeviceInfo.h"

#include <span>
#include <string>
#include <vector>

namespace asb::diagnostics {

[[nodiscard]] bool isRelevantHidDevice(const HidDeviceInfo& info);

[[nodiscard]] std::vector<HidDeviceInfo> selectHidDevices(
    std::span<const HidDeviceInfo> devices,
    bool includeAllHid);

[[nodiscard]] std::string formatHidDevicesText(std::span<const HidDeviceInfo> devices);
[[nodiscard]] std::string formatHidDevicesJson(std::span<const HidDeviceInfo> devices);

} // namespace asb::diagnostics
