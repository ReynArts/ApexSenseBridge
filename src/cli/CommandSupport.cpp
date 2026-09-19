#include "cli/Commands.h"
#include "cli/CommandSupport.h"
#include "core/TriggerResetGuard.h"
#include "core/RumbleResetGuard.h"
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
#else
#include <csignal>
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
#else
namespace {
// Storing to a lock-free atomic is async-signal-safe, so the handler can stay
// this small. Every command loop already polls g_stopRequested, which is what
// runs the trigger/rumble reset guards and the isolation rollback; without
// this, Ctrl+C on Linux would kill the process mid-session and leave the pad
// holding its last FORCEADAPT effect.
extern "C" void requestStop(int) {
    g_stopRequested.store(true, std::memory_order_relaxed);
}
} // namespace
#endif

void installConsoleHandler() {
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
    struct sigaction action {};
    action.sa_handler = requestStop;
    sigemptyset(&action.sa_mask);
    // No SA_RESTART: a blocking poll or read must return so the loop can see
    // the flag instead of resuming and waiting for the next report.
    action.sa_flags = 0;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGHUP, &action, nullptr);
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
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << value;
    return oss.str();
}

bool isDualSenseGamepadInterface(const asb::HidDeviceInfo& info) {
    return info.vendorId == 0x054C && info.productId == 0x0CE6 &&
           (info.interfaceNumber.empty() || info.interfaceNumber == L"MI_03") &&
           info.featureReportLength >= 46;
}

namespace {

bool sameDevicePath(std::wstring_view left, std::wstring_view right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(),
                      [](wchar_t lhs, wchar_t rhs) {
                          return std::towupper(lhs) == std::towupper(rhs);
                      });
}

bool wasPresentBefore(const std::vector<std::wstring>& paths,
                      std::wstring_view candidate) {
    return std::any_of(paths.begin(), paths.end(),
                       [candidate](const auto& path) {
                           return sameDevicePath(path, candidate);
                       });
}

} // namespace

std::vector<std::wstring> snapshotDualSensePaths() {
    std::string ignored;
    const auto devices = asb::platform::enumerateHidDevices(ignored);
    std::vector<std::wstring> paths;
    for (const auto& info : devices) {
        if (isDualSenseGamepadInterface(info)) paths.push_back(info.path);
    }
    return paths;
}

bool waitForNewVirtualDualSenseRemoval(
    const std::vector<std::wstring>& preexistingPaths,
    std::chrono::milliseconds timeout,
    std::string& error) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::optional<std::chrono::steady_clock::time_point> absentSince;
    do {
        std::string enumerationError;
        const auto devices = asb::platform::enumerateHidDevices(enumerationError);
        if (!enumerationError.empty()) {
            error = std::move(enumerationError);
        } else {
            const bool newInterfaceStillPresent = std::any_of(
                devices.begin(), devices.end(),
                [&preexistingPaths](const auto& info) {
                    return isDualSenseGamepadInterface(info) &&
                           !wasPresentBefore(preexistingPaths, info.path);
                });
            if (!newInterfaceStillPresent) {
                const auto now = std::chrono::steady_clock::now();
                if (!absentSince) absentSince = now;
                // Require stable absence rather than a single empty PnP scan;
                // the usbip removal notification can race the next attach.
                if (now - *absentSince >= std::chrono::milliseconds(150)) {
                    error.clear();
                    return true;
                }
            } else {
                absentSince.reset();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < deadline);

    if (error.empty()) {
        error = "The previous virtual DualSense HID interface remained present after cleanup.";
    }
    return false;
}

std::optional<asb::dualsense::DualSenseFirmwareInfo> readNewVirtualDualSenseFirmware(
    const std::vector<std::wstring>& preexistingPaths,
    std::chrono::milliseconds timeout,
    std::string& error) {
    using TransportPtr = std::unique_ptr<asb::platform::HidTransport,
                                         void (*)(asb::platform::HidTransport*)>;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string lastError;
    do {
        std::string enumerationError;
        const auto devices = asb::platform::enumerateHidDevices(enumerationError);
        if (!enumerationError.empty()) lastError = enumerationError;
        for (const auto& info : devices) {
            if (!isDualSenseGamepadInterface(info) ||
                wasPresentBefore(preexistingPaths, info.path)) {
                continue;
            }

            std::string openError;
            TransportPtr transport(asb::platform::createHidTransport(info, openError),
                                   asb::platform::destroyHidTransport);
            if (!transport) {
                lastError = std::move(openError);
                continue;
            }

            std::vector<std::uint8_t> report(info.featureReportLength, 0);
            report[0] = 0x20;
            std::string featureError;
            if (!transport->readFeatureReport(report, featureError)) {
                lastError = std::move(featureError);
                continue;
            }
            if (auto firmware = asb::dualsense::decodeFirmwareFeatureReport(report)) {
                return firmware;
            }
            lastError = "The virtual DualSense returned a malformed firmware feature report.";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < deadline);

    error = lastError.empty()
        ? "The newly-created virtual DualSense firmware interface was not found."
        : std::move(lastError);
    return std::nullopt;
}

std::string jsonEscape(std::string_view value) {
    std::ostringstream escaped;
    for (const unsigned char character : value) {
        switch (character) {
        case '"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\b': escaped << "\\b"; break;
        case '\f': escaped << "\\f"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:
            if (character < 0x20) {
                escaped << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                        << static_cast<unsigned int>(character) << std::dec;
            } else {
                escaped << static_cast<char>(character);
            }
        }
    }
    return escaped.str();
}

void printDevice(const asb::HidDeviceInfo& info, std::size_t index) {
    std::cout << "[" << index << "] "
              << (info.product.empty() ? "Flydigi controller" : narrowAscii(info.product)) << "\n"
              << "    VID:PID      " << hex16(info.vendorId) << ":" << hex16(info.productId) << "\n"
              << "    Usage page   " << hex16(info.usagePage) << "  usage " << hex16(info.usage) << "\n"
              << "    Reports      input=" << info.inputReportLength
              << " output=" << info.outputReportLength << " bytes\n";
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

std::optional<asb::flydigi::Apex5Device> openSelected(int argc, char** argv, std::string& error) {
    auto candidates = asb::flydigi::Apex5Device::findCandidates(error);
    if (!error.empty() && candidates.empty()) {
        return std::nullopt;
    }
    if (candidates.empty()) {
        error = "No APEX 4/5 vendor HID interface found. For Apex 4, use USB or "
                "the 2.4 GHz dongle in DInput mode; for Apex 5, wake the controller.";
        return std::nullopt;
    }

    std::size_t index = 0;
    if (const auto requested = parseIndex(argc, argv)) {
        index = *requested;
    } else if (candidates.size() > 1) {
        error = "More than one Flydigi controller vendor interface was found. Run 'list' and pass the wanted index.";
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
    auto candidates = asb::flydigi::Apex5Device::findCandidates(error);
    if (!error.empty() && candidates.empty()) return std::nullopt;
    if (candidates.empty()) {
        error = "No APEX 4/5 vendor HID interface found. For Apex 4, use USB or "
                "the 2.4 GHz dongle in DInput mode; for Apex 5, wake the controller.";
        return std::nullopt;
    }
    if (!requested && candidates.size() > 1) {
        error = "More than one Flydigi controller vendor interface was found. Run 'list' and pass the wanted index.";
        return std::nullopt;
    }
    const auto index = requested.value_or(0);
    if (index >= candidates.size()) {
        error = "Device index is out of range. Run 'list' first.";
        return std::nullopt;
    }
    auto device = asb::flydigi::Apex5Device::open(candidates[index], error);
    if (!device || !device->verifyIdentity(error)) return std::nullopt;
    return device;
}

} // namespace asb::cli
