#pragma once

#include "core/DeviceInfo.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace asb::diagnostics {

[[nodiscard]] std::vector<HidDeviceInfo> orderApexCandidates(std::vector<HidDeviceInfo> candidates);

[[nodiscard]] std::optional<std::size_t> firstVerifiedCandidate(
    std::size_t count,
    const std::function<bool(std::size_t)>& verify,
    const std::function<bool()>& shouldStop);

[[nodiscard]] bool isRelevantHidDevice(const HidDeviceInfo& info);

[[nodiscard]] std::vector<HidDeviceInfo> selectHidDevices(
    std::span<const HidDeviceInfo> devices,
    bool includeAllHid);

[[nodiscard]] std::string formatHidDevicesText(std::span<const HidDeviceInfo> devices);
[[nodiscard]] std::string formatHidDevicesJson(std::span<const HidDeviceInfo> devices);

[[nodiscard]] std::string hex16(std::uint16_t value);

} // namespace asb::diagnostics
