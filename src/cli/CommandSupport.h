#pragma once

#include "cli/DeviceOpenRetry.h"
#include "cli/JsonSupport.h"
#include "diagnostics/HidDiagnostics.h"
#include "dualsense/DualSenseFirmware.h"
#include "dualsense/DualSenseInput.h"
#include "dualsense/VirtualDualSenseStartup.h"
#include "flydigi/Apex5Device.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
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
bool waitForNewVirtualDualSenseRemoval(
    const std::vector<std::wstring>& preexistingPaths,
    std::chrono::milliseconds timeout,
    std::string& error);

using VirtualDualSenseDiscovery = asb::dualsense::VirtualDualSenseReadiness;

asb::dualsense::VirtualDualSenseProbeEnvironment makeVirtualDualSenseProbeEnvironment(
    std::function<bool()> stopRequested,
    const asb::dualsense::DualSenseInputState* expectedInitialInput = nullptr);

std::optional<VirtualDualSenseDiscovery> readNewVirtualDualSenseFirmware(
    const std::vector<std::wstring>& preexistingPaths,
    std::chrono::milliseconds timeout,
    std::string& error,
    const asb::dualsense::DualSenseInputState* expectedInitialInput = nullptr);
std::optional<asb::dualsense::VirtualDualSenseBackend> parseVirtualDualSenseBackend(
    std::string_view name);
void printDevice(const asb::HidDeviceInfo& info, std::size_t index);
std::optional<std::size_t> parseIndex(int argc, char** argv);
std::optional<asb::flydigi::Apex5Device> openSelected(
    int argc, char** argv, std::string& error);
std::optional<asb::flydigi::Apex5Device> openSelectedIndex(
    std::optional<std::size_t> requested, const std::function<bool()>& shouldStop,
    std::string& error, DeviceOpenFailure& failure);
std::optional<asb::flydigi::Apex5Device> openSelectedIndex(
    std::optional<std::size_t> requested, const std::function<bool()>& shouldStop,
    std::string& error);
std::optional<asb::flydigi::Apex5Device> openSelectedIndex(
    std::optional<std::size_t> requested, std::string& error);

struct CandidateSelection {
    std::size_t candidates = 0;
    std::size_t selected = 0;
};

std::vector<asb::HidDeviceInfo> orderedCandidates(std::string& error);
std::string severalControllersError(std::size_t count);
std::string describeCandidateSelection(const CandidateSelection& selection,
                                       const asb::flydigi::Apex5Device& device);
std::optional<asb::flydigi::Apex5Device> openFirstVerifiedCandidate(
    const std::function<bool()>& shouldStop, std::string& error,
    DeviceOpenFailure& failure, CandidateSelection& selection);

} // namespace asb::cli
