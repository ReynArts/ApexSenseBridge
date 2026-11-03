#include "cli/Commands.h"
#include "cli/CommandSupport.h"
#include "diagnostics/HidDiagnostics.h"
#include "dualsense/DualSenseFirmware.h"
#include "dualsense/VirtualDualSense.h"
#include "dualsense/AdaptiveTriggerBridge.h"
#include "dualsense/AdaptiveTriggerTranslation.h"
#include "dualsense/RumbleBridge.h"
#include "dualsense/TouchpadGestureProfile.h"
#include "flydigi/Apex5Device.h"
#include "flydigi/Apex5Protocol.h"
#include "platform/HidTransport.h"
#include "platform/AudioEndpointProtection.h"
#include "platform/PhysicalControllerIsolation.h"
#include "platform/PhysicalInputSource.h"
#include "platform/SessionControl.h"
#include "platform/XInputGamepad.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace asb::cli {

std::atomic_bool g_stopRequested{false};

#ifdef _WIN32
namespace {
BOOL WINAPI consoleHandler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT ||
        event == CTRL_CLOSE_EVENT || event == CTRL_LOGOFF_EVENT ||
        event == CTRL_SHUTDOWN_EVENT) {
        g_stopRequested.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}
} // namespace
#endif

void installConsoleHandler() {
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#endif
}

std::string narrowAscii(const std::wstring& value) {
    std::string out;
    out.reserve(value.size());
    for (const wchar_t ch : value) {
        out.push_back(ch >= 32 && ch <= 126 ? static_cast<char>(ch) : '?');
    }
    return out;
}

std::string hex16(std::uint16_t value) {
    return asb::diagnostics::hex16(value);
}

bool isDualSenseGamepadInterface(const asb::HidDeviceInfo& info) {
    return asb::dualsense::isDualSenseGamepadCollection(info);
}

std::vector<std::wstring> snapshotDualSensePaths() {
    std::string ignored;
    const auto devices = asb::platform::enumerateHidDevices(ignored);
    std::vector<std::wstring> paths;
    for (const auto& info : devices) {
        if (asb::dualsense::isDualSenseHidInterface(info)) paths.push_back(info.path);
    }
    return paths;
}

asb::dualsense::VirtualDualSenseProbeEnvironment makeVirtualDualSenseProbeEnvironment(
    std::function<bool()> stopRequested,
    const asb::dualsense::DualSenseInputState* expectedInitialInput) {
    using TransportPtr = std::unique_ptr<asb::platform::HidTransport,
                                         void (*)(asb::platform::HidTransport*)>;
    asb::dualsense::VirtualDualSenseProbeEnvironment environment{};
    environment.enumerate = [](std::string& enumerationError) {
        return asb::platform::enumerateHidDevices(enumerationError);
    };
    environment.verify = [expectedInitialInput](const asb::HidDeviceInfo& info,
                                                std::string& lastError)
        -> std::optional<VirtualDualSenseDiscovery> {
        std::string openError;
        TransportPtr transport(asb::platform::createHidTransport(info, openError),
                               asb::platform::destroyHidTransport);
        if (!transport) {
            lastError = std::move(openError);
            return std::nullopt;
        }

        std::vector<std::uint8_t> report(info.featureReportLength, 0);
        report[0] = 0x20;
        std::string featureError;
        if (!transport->readFeatureReport(report, featureError)) {
            lastError = std::move(featureError);
            return std::nullopt;
        }
        auto firmware = asb::dualsense::decodeFirmwareFeatureReport(report);
        if (!firmware) {
            lastError = "The virtual DualSense returned a malformed firmware feature report.";
            return std::nullopt;
        }

        std::size_t verifiedInitialReports = 0;
        if (expectedInitialInput) {
            std::vector<std::uint8_t> inputBuffer(64, 0);
            const auto inputDeadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
            while (verifiedInitialReports < 3 &&
                   std::chrono::steady_clock::now() < inputDeadline) {
                std::size_t bytesRead = 0;
                std::string inputError;
                const auto status = transport->readInputReport(
                    inputBuffer, std::chrono::milliseconds(40), bytesRead, inputError);
                if (status == asb::platform::HidReadStatus::Data &&
                    bytesRead >= 11 && inputBuffer[0] == 0x01) {
                    const bool axesMatch =
                        inputBuffer[1] == expectedInitialInput->lx &&
                        inputBuffer[2] == expectedInitialInput->ly &&
                        inputBuffer[3] == expectedInitialInput->rx &&
                        inputBuffer[4] == expectedInitialInput->ry &&
                        inputBuffer[5] == expectedInitialInput->l2 &&
                        inputBuffer[6] == expectedInitialInput->r2;
                    if (axesMatch) {
                        ++verifiedInitialReports;
                    }
                } else if (status == asb::platform::HidReadStatus::Error) {
                    lastError = "Initial HID input report read failed: " + inputError;
                    return std::nullopt;
                }
            }
            if (verifiedInitialReports < 3) {
                lastError = "The virtual DualSense HID interface did not produce verified initial input reports.";
                return std::nullopt;
            }
        }

        return VirtualDualSenseDiscovery(*firmware, info, verifiedInitialReports);
    };
    environment.explainUnreadable = [](const asb::HidDeviceInfo& info) {
        std::string openError;
        TransportPtr transport(asb::platform::createHidTransport(info, openError),
                               asb::platform::destroyHidTransport);
        return transport
            ? std::string("the interface opened, but its HID capabilities were not yet readable")
            : openError;
    };
    environment.stopRequested = std::move(stopRequested);
    return environment;
}

bool waitForNewVirtualDualSenseRemoval(
    const std::vector<std::wstring>& preexistingPaths,
    std::chrono::milliseconds timeout,
    std::string& error) {
    asb::dualsense::VirtualDualSenseBaseline baseline(preexistingPaths);
    return asb::dualsense::waitForVirtualDualSenseRemoval(
        baseline, makeVirtualDualSenseProbeEnvironment({}, nullptr), timeout,
        std::chrono::milliseconds(150), error);
}

std::optional<VirtualDualSenseDiscovery> readNewVirtualDualSenseFirmware(
    const std::vector<std::wstring>& preexistingPaths,
    std::chrono::milliseconds timeout,
    std::string& error,
    const asb::dualsense::DualSenseInputState* expectedInitialInput) {
    asb::dualsense::VirtualDualSenseBaseline baseline(preexistingPaths);
    asb::dualsense::VirtualDualSenseProbeTiming timing{};
    timing.quietWindow = timeout;
    timing.progressWindow = timeout;
    asb::dualsense::VirtualDualSenseProbeReport report{};
    return asb::dualsense::probeNewVirtualDualSense(
        baseline, timing, makeVirtualDualSenseProbeEnvironment({}, expectedInitialInput),
        report, error);
}

void printDevice(const asb::HidDeviceInfo& info, std::size_t index) {
    std::cout << "[" << index << "] "
              << (info.product.empty() ? "Flydigi controller" : narrowAscii(info.product)) << "\n"
              << "    VID:PID      " << hex16(info.vendorId) << ":" << hex16(info.productId) << "\n"
              << "    Usage page   " << hex16(info.usagePage) << "  usage " << hex16(info.usage) << "\n"
              << "    Reports      input=" << info.inputReportLength
              << " output=" << info.outputReportLength << " bytes\n"
              << "    Path         " << narrowAscii(info.path) << '\n';
}

std::optional<std::size_t> parseIndex(int argc, char** argv) {
    if (argc < 3) {
        return std::nullopt;
    }
    try {
        return static_cast<std::size_t>(std::stoul(argv[2]));
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<asb::HidDeviceInfo> orderedCandidates(std::string& error) {
    return asb::diagnostics::orderApexCandidates(
        asb::flydigi::Apex5Device::findCandidates(error));
}

std::string severalControllersError(std::size_t count) {
    return "Several APEX controllers are connected (" + std::to_string(count) +
           " vendor interfaces). ApexSenseBridge drives one controller at a time: "
           "switch off or unplug the other controllers, or pass the index printed by "
           "the 'ApexSenseBridge.exe list' command.";
}

std::string describeCandidateSelection(const CandidateSelection& selection,
                                       const asb::flydigi::Apex5Device& device) {
    std::ostringstream output;
    output << "Several APEX controllers are connected (" << selection.candidates
           << " vendor interfaces); bridging candidate [" << selection.selected << "] ";
    if (device.identity()) {
        output << device.identity()->describe() << " over "
               << (device.identity()->isWired() ? "USB cable" : "2.4 GHz dongle");
    } else {
        output << "APEX";
    }
    output << " at " << narrowAscii(device.info().path)
           << ". ApexSenseBridge bridges one controller at a time: the first "
              "interface in 'list' order that answers the identity check.";
    return output.str();
}

std::optional<asb::flydigi::Apex5Device> openFirstVerifiedCandidate(
    const std::function<bool()>& shouldStop, std::string& error,
    DeviceOpenFailure& failure, CandidateSelection& selection) {
    failure = DeviceOpenFailure::Other;
    selection = {};
    const auto candidates = orderedCandidates(error);
    if (candidates.size() <= 1) {
        error.clear();
        return openSelectedIndex(std::nullopt, shouldStop, error, failure);
    }
    error.clear();
    selection.candidates = candidates.size();
    std::optional<asb::flydigi::Apex5Device> device;
    std::size_t unanswered = 0;
    std::string lastError;
    const auto chosen = asb::diagnostics::firstVerifiedCandidate(
        candidates.size(),
        [&](std::size_t index) {
            std::string attemptError;
            auto opened = asb::flydigi::Apex5Device::open(candidates[index], attemptError);
            if (opened && opened->verifyIdentity(attemptError, shouldStop)) {
                device = std::move(opened);
                return true;
            }
            if (opened && opened->identityUnanswered()) ++unanswered;
            lastError = "[" + std::to_string(index) + "] " + attemptError;
            return false;
        },
        shouldStop);
    if (!chosen) {
        if (unanswered == candidates.size()) failure = DeviceOpenFailure::IdentityUnanswered;
        error = "None of the " + std::to_string(candidates.size()) +
                " APEX vendor interfaces answered the identity check; last error: " + lastError;
        return std::nullopt;
    }
    selection.selected = *chosen;
    return device;
}

std::optional<asb::flydigi::Apex5Device> openSelected(int argc, char** argv, std::string& error) {
    auto candidates = orderedCandidates(error);
    if (!error.empty() && candidates.empty()) {
        return std::nullopt;
    }
    if (candidates.empty()) {
        error = "No APEX 4/5/6 vendor HID interface found. For Apex 4, use USB or "
                "the 2.4 GHz dongle in DInput mode; for Apex 5, wake the controller.";
        return std::nullopt;
    }

    std::size_t index = 0;
    if (const auto requested = parseIndex(argc, argv)) {
        index = *requested;
    } else if (candidates.size() > 1) {
        error = severalControllersError(candidates.size());
        return std::nullopt;
    }

    if (index >= candidates.size()) {
        error = "Device index is out of range. Run 'list' first.";
        return std::nullopt;
    }

    auto device = asb::flydigi::Apex5Device::open(candidates[index], error);
    if (!device || !device->verifyIdentity(error)) {
        return std::nullopt;
    }
    return device;
}

std::optional<asb::flydigi::Apex5Device> openSelectedIndex(
    std::optional<std::size_t> requested, std::string& error) {
    return openSelectedIndex(requested, std::function<bool()>{}, error);
}

std::optional<asb::flydigi::Apex5Device> openSelectedIndex(
    std::optional<std::size_t> requested, const std::function<bool()>& shouldStop,
    std::string& error) {
    DeviceOpenFailure failure = DeviceOpenFailure::Other;
    return openSelectedIndex(requested, shouldStop, error, failure);
}

std::optional<asb::flydigi::Apex5Device> openSelectedIndex(
    std::optional<std::size_t> requested, const std::function<bool()>& shouldStop,
    std::string& error, DeviceOpenFailure& failure) {
    failure = DeviceOpenFailure::Other;
    auto candidates = orderedCandidates(error);
    if (!error.empty() && candidates.empty()) return std::nullopt;
    if (candidates.empty()) {
        error = "No APEX 4/5/6 vendor HID interface found. For Apex 4, use USB or "
                "the 2.4 GHz dongle in DInput mode; for Apex 5, wake the controller.";
        return std::nullopt;
    }
    if (!requested && candidates.size() > 1) {
        error = severalControllersError(candidates.size());
        return std::nullopt;
    }
    const auto index = requested.value_or(0);
    if (index >= candidates.size()) {
        error = "Device index is out of range. Run 'list' first.";
        return std::nullopt;
    }
    auto device = asb::flydigi::Apex5Device::open(candidates[index], error);
    if (!device) return std::nullopt;
    if (!device->verifyIdentity(error, shouldStop)) {
        if (device->identityUnanswered()) failure = DeviceOpenFailure::IdentityUnanswered;
        return std::nullopt;
    }
    return device;
}

} // namespace asb::cli
