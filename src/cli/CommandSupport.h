#pragma once

#include "diagnostics/HidDiagnostics.h"
#include "dualsense/DualSenseFirmware.h"
#include "dualsense/VirtualDualSense.h"
#include "flydigi/Apex5Device.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asb::cli {

extern std::atomic_bool g_stopRequested;

void installConsoleHandler();
std::string narrowAscii(const std::wstring& value);
std::string hex16(std::uint16_t value);
bool isDualSenseGamepadInterface(const asb::HidDeviceInfo& info);
std::vector<std::wstring> snapshotDualSensePaths();
std::optional<asb::dualsense::DualSenseFirmwareInfo> readNewVirtualDualSenseFirmware(
    const std::vector<std::wstring>& preexistingPaths,
    std::chrono::milliseconds timeout,
    std::string& error);
std::optional<asb::dualsense::VirtualDualSenseBackend> parseVirtualDualSenseBackend(
    std::string_view name);
std::string jsonEscape(std::string_view value);
void printDevice(const asb::HidDeviceInfo& info, std::size_t index);
std::optional<std::size_t> parseIndex(int argc, char** argv);
std::optional<asb::flydigi::Apex5Device> openSelected(
    int argc, char** argv, std::string& error);
std::optional<asb::flydigi::Apex5Device> openSelectedIndex(
    std::optional<std::size_t> requested, std::string& error);

} // namespace asb::cli
