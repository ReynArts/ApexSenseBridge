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

namespace {

std::atomic_bool g_stopRequested{false};

class MicrosecondLatencyHistogram {
public:
    void observe(std::chrono::steady_clock::duration duration) noexcept {
        const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
            duration).count();
        const auto bucket = static_cast<std::size_t>((std::clamp)(
            microseconds, std::int64_t{0},
            static_cast<std::int64_t>(buckets_.size() - 1)));
        ++buckets_[bucket];
        ++samples_;
    }

    [[nodiscard]] std::uint64_t percentile(unsigned int percentage) const noexcept {
        if (samples_ == 0) return 0;
        const auto wanted = (samples_ * percentage + 99) / 100;
        std::uint64_t cumulative = 0;
        for (std::size_t index = 0; index < buckets_.size(); ++index) {
            cumulative += buckets_[index];
            if (cumulative >= wanted) return index;
        }
        return buckets_.size() - 1;
    }

    [[nodiscard]] std::uint64_t samples() const noexcept { return samples_; }

private:
    // The final bucket includes every value >= 2 ms. The acceptance target is
    // 1.5 ms, so this fixed 16 KiB structure gives useful resolution without
    // allocating or sorting samples in the hot input path.
    std::array<std::uint64_t, 2001> buckets_{};
    std::uint64_t samples_ = 0;
};

struct ProcessUsageSnapshot {
    std::uint64_t cpu100ns = 0;
    std::uint64_t workingSetBytes = 0;
    std::uint64_t peakWorkingSetBytes = 0;
};

ProcessUsageSnapshot processUsageSnapshot() noexcept {
    ProcessUsageSnapshot snapshot{};
#ifdef _WIN32
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        ULARGE_INTEGER kernelValue{};
        kernelValue.LowPart = kernel.dwLowDateTime;
        kernelValue.HighPart = kernel.dwHighDateTime;
        ULARGE_INTEGER userValue{};
        userValue.LowPart = user.dwLowDateTime;
        userValue.HighPart = user.dwHighDateTime;
        snapshot.cpu100ns = kernelValue.QuadPart + userValue.QuadPart;
    }
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        snapshot.workingSetBytes = counters.WorkingSetSize;
        snapshot.peakWorkingSetBytes = counters.PeakWorkingSetSize;
    }
#endif
    return snapshot;
}

unsigned int logicalProcessorCount() noexcept {
#ifdef _WIN32
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return (std::max)(1U, static_cast<unsigned int>(info.dwNumberOfProcessors));
#else
    return (std::max)(1U, std::thread::hardware_concurrency());
#endif
}

bool gameplayControlsReleased(
    const asb::dualsense::DualSenseInputState& state) noexcept {
    constexpr std::uint8_t kTriggerReleaseThreshold = 8;
    return state.buttons == 0 && state.dpad == 0 &&
           state.l2 <= kTriggerReleaseThreshold &&
           state.r2 <= kTriggerReleaseThreshold;
}

bool waitForPhysicalControlsReleased(
    asb::platform::PhysicalInputSource& input,
    std::chrono::milliseconds maximumWait) noexcept {
    constexpr auto kStableRelease = std::chrono::milliseconds(120);
    const auto deadline = std::chrono::steady_clock::now() + maximumWait;
    std::optional<std::chrono::steady_clock::time_point> releasedAt;
    while (std::chrono::steady_clock::now() < deadline) {
        asb::dualsense::DualSenseInputState state{};
        std::string error;
        const auto status = input.waitForState(
            state, input.eventDriven() ? std::chrono::milliseconds(25)
                                       : std::chrono::milliseconds(1),
            error);
        const auto now = std::chrono::steady_clock::now();
        if (status == asb::platform::PhysicalInputStatus::State) {
            if (gameplayControlsReleased(state)) {
                if (!releasedAt) releasedAt = now;
                if (now - *releasedAt >= kStableRelease) return true;
            } else {
                releasedAt.reset();
            }
        } else if (status == asb::platform::PhysicalInputStatus::Disconnected ||
                   status == asb::platform::PhysicalInputStatus::Error) {
            return false;
        }
    }
    return false;
}

class ButtonHoldTracker {
public:
    using Clock = std::chrono::steady_clock;

    void observe(bool pressed, Clock::time_point now) noexcept {
        if (pressed) {
            if (!pressedAt_) {
                pressedAt_ = now;
                ++presses_;
            }
            updateMaximum(now);
            return;
        }
        finish(now);
    }

    void finish(Clock::time_point now) noexcept {
        if (!pressedAt_) return;
        updateMaximum(now);
        pressedAt_.reset();
    }

    [[nodiscard]] std::uint64_t presses() const noexcept { return presses_; }
    [[nodiscard]] std::int64_t maximumHoldMilliseconds() const noexcept {
        return maximumHold_.count();
    }

private:
    void updateMaximum(Clock::time_point now) noexcept {
        if (!pressedAt_) return;
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - *pressedAt_);
        if (duration > maximumHold_) maximumHold_ = duration;
    }

    std::optional<Clock::time_point> pressedAt_;
    std::chrono::milliseconds maximumHold_{};
    std::uint64_t presses_ = 0;
};

#ifdef _WIN32
BOOL WINAPI consoleHandler(DWORD event) {
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        g_stopRequested.store(true, std::memory_order_relaxed);
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

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

std::vector<std::wstring> snapshotDualSensePaths() {
    std::string ignored;
    const auto devices = asb::platform::enumerateHidDevices(ignored);
    std::vector<std::wstring> paths;
    for (const auto& info : devices) {
        if (isDualSenseGamepadInterface(info)) paths.push_back(info.path);
    }
    return paths;
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
                std::find(preexistingPaths.begin(), preexistingPaths.end(), info.path) !=
                    preexistingPaths.end()) {
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

void printUsage() {
    std::cout
        << "ApexSenseBridge 0.5.0\n\n"
        << "Commands:\n"
        << "  list                         List APEX 5 vendor HID candidates\n"
        << "  diagnose [--all-hid] [--json]\n"
        << "                               Read-only HID interface diagnostic\n"
        << "  input-status [index] [--seconds N] [--json]\n"
        << "                               Validate the complete APEX input proxy source\n"
        << "  stop-active-sessions         Gracefully detach/restore an active bridge\n"
        << "  identify [index]             Read and verify Flydigi command 0x01 identity\n"
        << "  virtual-ds [--seconds N] [--json] [--viiper PATH] [--virtual-backend NAME]\n"
        << "                               Create a neutral virtual DualSense and count feedback\n"
        << "  bridge-triggers [index] [--seconds N] [--viiper PATH]\n"
        << "                  [--telemetry-json PATH]\n"
        << "                  [--proxy-xinput] [--xinput-index 0..3]\n"
        << "                  [--rumble]\n"
        << "                  [--haptic-threshold 0..95]\n"
        << "                  [--verify-virtual-input]\n"
        << "                  [--virtual-backend auto|integrated|sidecar]\n"
        << "                  [--touchpad-profile NAME]\n"
        << "                  [--view-hold-swipe-up]\n"
        << "                  [--isolate-apex]\n"
        << "                  [--session-token 32HEX]\n"
        << "                               Route adaptive triggers and optional grip/audio haptics\n"
        << "  test-rt [index]              Gentle RT FORCEADAPT test (~1.5 s)\n"
        << "  test-rumble [index]          Gentle grip-motor vibration test (~1 s)\n"
        << "  xinput-view-test [index] [--seconds N]\n"
        << "                               Measure View/Back hold duration without writes\n"
        << "  clear [index]                Clear LT/RT effects and stop grip rumble\n"
        << "  dry-run                      Print the test packet without HID I/O\n\n"
        << "The hardware-writing commands only target Flydigi VID 37D7, controller PID family 2xxx,\n"
        << "and vendor usage page FFA0. If more than one candidate is found, pass its list index.\n"
        << "virtual-ds never opens the APEX HID interface and never routes feedback to it.\n";
}

struct VirtualDsCommandOptions {
    std::optional<std::chrono::seconds> duration;
    bool json = false;
    std::filesystem::path viiperExecutable;
    asb::dualsense::VirtualDualSenseBackend backend =
        asb::dualsense::VirtualDualSenseBackend::Auto;
};

std::optional<asb::dualsense::VirtualDualSenseBackend> parseVirtualDualSenseBackend(
    std::string_view name) {
    if (name == "auto") return asb::dualsense::VirtualDualSenseBackend::Auto;
    if (name == "integrated") return asb::dualsense::VirtualDualSenseBackend::Integrated;
    if (name == "sidecar") return asb::dualsense::VirtualDualSenseBackend::Sidecar;
    return std::nullopt;
}

bool parseVirtualDsOptions(int argc,
                           char** argv,
                           VirtualDsCommandOptions& options,
                           std::string& error) {
    for (int index = 2; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--json") {
            options.json = true;
        } else if (option == "--seconds") {
            if (++index >= argc) {
                error = "--seconds requires an integer from 1 to 86400.";
                return false;
            }
            try {
                const unsigned long seconds = std::stoul(argv[index]);
                if (seconds == 0 || seconds > 86400) {
                    throw std::out_of_range("seconds");
                }
                options.duration = std::chrono::seconds(seconds);
            } catch (...) {
                error = "--seconds requires an integer from 1 to 86400.";
                return false;
            }
        } else if (option == "--viiper") {
            if (++index >= argc) {
                error = "--viiper requires a path to the patched viiper.exe.";
                return false;
            }
            options.viiperExecutable = std::filesystem::path(argv[index]);
        } else if (option == "--virtual-backend") {
            if (++index >= argc) {
                error = "--virtual-backend requires auto, integrated, or sidecar.";
                return false;
            }
            const auto backend = parseVirtualDualSenseBackend(argv[index]);
            if (!backend) {
                error = "--virtual-backend requires auto, integrated, or sidecar.";
                return false;
            }
            options.backend = *backend;
        } else {
            error = "Unknown virtual-ds option: " + std::string(option);
            return false;
        }
    }
    return true;
}

void printVirtualDsStats(const asb::dualsense::VirtualDualSenseStats& stats,
                         bool json,
                         asb::platform::AudioDefaultProtectionStatus audioStatus,
                         std::size_t restoredAudioRoles,
                         const std::optional<asb::dualsense::DualSenseFirmwareInfo>& firmware) {
    if (json) {
        std::cout
            << "{\n"
            << "  \"virtual_ds_connected\": " << (stats.connected ? "true" : "false") << ",\n"
            << "  \"backend\": \"VIIPER\",\n"
            << "  \"backend_version\": \"" << jsonEscape(stats.backendVersion) << "\",\n"
            << "  \"dualsense_firmware_update\": \""
            << (firmware ? hex16(firmware->updateVersion) : "unavailable") << "\",\n"
            << "  \"dualsense_firmware_current\": "
            << (firmware && firmware->updateVersion >= 0x0630 ? "true" : "false") << ",\n"
            << "  \"input_mode\": \"neutral-static\",\n"
            << "  \"apex_routing\": \"disabled\",\n"
            << "  \"output_reports\": " << stats.outputReports << ",\n"
            << "  \"trigger_reports\": " << stats.triggerReports << ",\n"
            << "  \"rumble_reports\": " << stats.rumbleReports << ",\n"
            << "  \"audio_haptics_frames\": " << stats.audioHapticsFrames << ",\n"
            << "  \"audio_default_protection\": \""
            << asb::platform::audioDefaultProtectionStatusName(audioStatus) << "\",\n"
            << "  \"audio_default_roles_restored\": " << restoredAudioRoles << ",\n"
            << "  \"malformed_frames\": " << stats.malformedFrames << ",\n"
            << "  \"unknown_frames\": " << stats.unknownFrames << "\n"
            << "}\n";
        return;
    }

    std::cout
        << "virtual_ds_connected=" << (stats.connected ? "yes" : "no") << '\n'
        << "backend=VIIPER " << stats.backendVersion << '\n'
        << "dualsense_firmware_update="
        << (firmware ? hex16(firmware->updateVersion) : "unavailable") << '\n'
        << "dualsense_firmware_current="
        << (firmware && firmware->updateVersion >= 0x0630 ? "yes" : "no") << '\n'
        << "input_mode=neutral-static\n"
        << "apex_routing=disabled\n"
        << "output_reports=" << stats.outputReports << '\n'
        << "trigger_reports=" << stats.triggerReports << '\n'
        << "rumble_reports=" << stats.rumbleReports << '\n'
        << "audio_haptics_frames=" << stats.audioHapticsFrames << '\n'
        << "audio_default_protection="
        << asb::platform::audioDefaultProtectionStatusName(audioStatus) << '\n'
        << "audio_default_roles_restored=" << restoredAudioRoles << '\n'
        << "malformed_frames=" << stats.malformedFrames << '\n'
        << "unknown_frames=" << stats.unknownFrames << '\n';
}

int commandVirtualDs(int argc, char** argv) {
    VirtualDsCommandOptions commandOptions{};
    std::string error;
    if (!parseVirtualDsOptions(argc, argv, commandOptions, error)) {
        std::cerr << error << "\nUsage: ApexSenseBridge virtual-ds [--seconds N] [--json] [--viiper PATH] [--virtual-backend auto|integrated|sidecar]\n";
        return 1;
    }

    const auto preexistingDualSensePaths = snapshotDualSensePaths();
    asb::platform::VirtualDualSenseAudioEndpointProtection audioProtection;
    std::string audioProtectionError;
    if (!audioProtection.capture(audioProtectionError) && !commandOptions.json) {
        std::cerr << "Warning: Windows default-audio protection is unavailable: "
                  << audioProtectionError << '\n';
    }

    asb::dualsense::VirtualDualSenseOptions backendOptions{};
    backendOptions.viiperExecutable = std::move(commandOptions.viiperExecutable);
    backendOptions.backend = commandOptions.backend;
    auto virtualDualSense = asb::dualsense::createVirtualDualSense(std::move(backendOptions));
    if (!virtualDualSense->open(error)) {
        if (commandOptions.json) {
            std::cout << "{\"virtual_ds_connected\":false,\"apex_routing\":\"disabled\",\"error\":\""
                      << jsonEscape(error) << "\"}\n";
        } else {
            std::cerr << "Virtual DualSense creation failed: " << error << '\n'
                      << "The APEX controller was not opened or modified.\n";
        }
        return 6;
    }

    if (audioProtection.captured() &&
        !audioProtection.protectAfterVirtualDualSenseStart(
            std::chrono::milliseconds(2000), audioProtectionError) &&
        !commandOptions.json) {
        std::cerr << "Warning: Windows default-audio protection failed: "
                  << audioProtectionError << '\n';
    }

    std::string firmwareError;
    const auto firmware = readNewVirtualDualSenseFirmware(
        preexistingDualSensePaths, std::chrono::milliseconds(1500), firmwareError);
    if (!firmware && !commandOptions.json) {
        std::cerr << "Warning: virtual DualSense firmware verification failed: "
                  << firmwareError << '\n';
    }

    if (!commandOptions.json) {
        const auto initialStats = virtualDualSense->stats();
        std::cout << "Virtual DualSense connected through VIIPER " << initialStats.backendVersion << ".\n"
                  << (firmware
                          ? "Virtual DualSense firmware " + hex16(firmware->updateVersion) +
                                (firmware->updateVersion >= 0x0630 ? " verified.\n"
                                                                  : " is obsolete.\n")
                          : "")
                  << "APEX routing is disabled; only a static neutral input state is exposed.\n"
                  << (audioProtection.status() ==
                              asb::platform::AudioDefaultProtectionStatus::Restored
                          ? "Windows default playback was restored to the original device; DualSense haptic audio remains available.\n"
                          : "")
                  << (commandOptions.duration ? "Capturing feedback...\n"
                                              : "Capturing feedback; press Ctrl+C to stop cleanly.\n");
    }

    const auto started = std::chrono::steady_clock::now();
    bool streamDisconnected = false;
    while (!g_stopRequested.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!virtualDualSense->connected()) {
            streamDisconnected = true;
            break;
        }
        if (commandOptions.duration &&
            std::chrono::steady_clock::now() - started >= *commandOptions.duration) {
            break;
        }
    }

    const auto finalStats = virtualDualSense->stats();
    virtualDualSense->close();
    printVirtualDsStats(finalStats, commandOptions.json,
                        audioProtection.status(), audioProtection.restoredRoles(), firmware);
    if (streamDisconnected) {
        if (!commandOptions.json) {
            std::cerr << "The VIIPER feedback stream disconnected unexpectedly.\n";
        }
        return 7;
    }
    return 0;
}

int commandDiagnose(int argc, char** argv) {
    bool includeAllHid = false;
    bool json = false;
    for (int index = 2; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--all-hid") {
            includeAllHid = true;
        } else if (option == "--json") {
            json = true;
        } else {
            std::cerr << "Unknown diagnose option: " << option << "\n"
                      << "Usage: ApexSenseBridge diagnose [--all-hid] [--json]\n";
            return 1;
        }
    }

    std::string error;
    const auto allDevices = asb::platform::enumerateHidDevices(error);
    const auto selected = asb::diagnostics::selectHidDevices(allDevices, includeAllHid);

    if (!error.empty()) {
        std::cerr << "HID enumeration warning: " << error << '\n';
    }

    if (json) {
        std::cout << asb::diagnostics::formatHidDevicesJson(selected);
    } else {
        std::cout << (includeAllHid ? "Mode: all HID interfaces\n" : "Mode: relevant HID interfaces\n")
                  << asb::diagnostics::formatHidDevicesText(selected);
    }

    return !error.empty() && allDevices.empty() ? 2 : 0;
}

std::optional<asb::flydigi::Apex5Device> openSelectedIndex(
    std::optional<std::size_t> requested, std::string& error);

int commandInputStatus(int argc, char** argv) {
    std::optional<std::size_t> deviceIndex;
    unsigned long seconds = 3;
    bool json = false;
    for (int index = 2; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--json") {
            json = true;
        } else if (option == "--seconds") {
            if (++index >= argc) {
                std::cerr << "--seconds requires an integer from 1 to 60.\n";
                return 1;
            }
            try {
                std::size_t parsedCharacters = 0;
                seconds = std::stoul(argv[index], &parsedCharacters);
                if (parsedCharacters != std::string_view(argv[index]).size() ||
                    seconds == 0 || seconds > 60) {
                    throw std::out_of_range("seconds");
                }
            } catch (...) {
                std::cerr << "--seconds requires an integer from 1 to 60.\n";
                return 1;
            }
        } else {
            try {
                std::size_t parsedCharacters = 0;
                const auto parsed = std::stoul(std::string(option), &parsedCharacters);
                if (parsedCharacters != option.size() || deviceIndex) {
                    throw std::invalid_argument("index");
                }
                deviceIndex = static_cast<std::size_t>(parsed);
            } catch (...) {
                std::cerr << "Unknown input-status option: " << option << "\n"
                          << "Usage: ApexSenseBridge input-status [index] [--seconds N] [--json]\n";
                return 1;
            }
        }
    }

    std::string error;
    auto device = openSelectedIndex(deviceIndex, error);
    if (!device) {
        std::cerr << "APEX identity verification failed: " << error << '\n';
        return 2;
    }

    error.clear();
    auto input = asb::platform::openPhysicalInputSource(device->info(), std::nullopt, error);
    if (!input) {
        std::cerr << "Complete APEX input source unavailable: " << error << '\n';
        return 3;
    }
    const std::string warning = error;

    asb::dualsense::DualSenseInputState lastState{};
    std::uint64_t stateChanges = 0;
    bool receivedState = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline &&
           !g_stopRequested.load(std::memory_order_relaxed)) {
        asb::dualsense::DualSenseInputState state{};
        error.clear();
        const auto status = input->waitForState(state, std::chrono::milliseconds(250), error);
        if (status == asb::platform::PhysicalInputStatus::State) {
            if (!receivedState || state != lastState) ++stateChanges;
            lastState = state;
            receivedState = true;
        } else if (status == asb::platform::PhysicalInputStatus::Disconnected ||
                   status == asb::platform::PhysicalInputStatus::Error) {
            std::cerr << "APEX input stream failed: " << error << '\n';
            return 4;
        }
    }

    const auto stats = input->stats();
    if (json) {
        std::cout
            << "{\n"
            << "  \"backend\": \"" << jsonEscape(input->backendName()) << "\",\n"
            << "  \"event_driven\": " << (input->eventDriven() ? "true" : "false") << ",\n"
            << "  \"received_state\": " << (receivedState ? "true" : "false") << ",\n"
            << "  \"reports\": " << stats.reports << ",\n"
            << "  \"state_changes\": " << stateChanges << ",\n"
            << "  \"timeouts\": " << stats.timeouts << ",\n"
            << "  \"parse_failures\": " << stats.parseFailures << ",\n"
            << "  \"lx\": " << static_cast<unsigned int>(lastState.lx) << ",\n"
            << "  \"ly\": " << static_cast<unsigned int>(lastState.ly) << ",\n"
            << "  \"rx\": " << static_cast<unsigned int>(lastState.rx) << ",\n"
            << "  \"ry\": " << static_cast<unsigned int>(lastState.ry) << ",\n"
            << "  \"l2\": " << static_cast<unsigned int>(lastState.l2) << ",\n"
            << "  \"r2\": " << static_cast<unsigned int>(lastState.r2) << ",\n"
            << "  \"dpad\": " << static_cast<unsigned int>(lastState.dpad) << ",\n"
            << "  \"buttons\": " << lastState.buttons << ",\n"
            << "  \"warning\": \"" << jsonEscape(warning) << "\"\n"
            << "}\n";
    } else {
        std::cout << "backend=" << input->backendName() << '\n'
                  << "event_driven=" << (input->eventDriven() ? "yes" : "no") << '\n'
                  << "received_state=" << (receivedState ? "yes" : "no") << '\n'
                  << "reports=" << stats.reports << '\n'
                  << "state_changes=" << stateChanges << '\n'
                  << "timeouts=" << stats.timeouts << '\n'
                  << "parse_failures=" << stats.parseFailures << '\n'
                  << "sticks=" << static_cast<unsigned int>(lastState.lx) << ','
                  << static_cast<unsigned int>(lastState.ly) << ','
                  << static_cast<unsigned int>(lastState.rx) << ','
                  << static_cast<unsigned int>(lastState.ry) << '\n'
                  << "triggers=" << static_cast<unsigned int>(lastState.l2) << ','
                  << static_cast<unsigned int>(lastState.r2) << '\n'
                  << "dpad=" << static_cast<unsigned int>(lastState.dpad) << '\n'
                  << "buttons=" << lastState.buttons << '\n';
        if (!warning.empty()) std::cout << "warning=" << warning << '\n';
    }
    return receivedState ? 0 : 5;
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
        error = "No APEX 5 vendor HID interface found. Wake the controller and keep the 2.4 GHz dongle connected.";
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
        error = "No APEX 5 vendor HID interface found. Wake the controller and keep the 2.4 GHz dongle connected.";
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

struct BridgeCommandOptions {
    std::optional<std::size_t> deviceIndex;
    std::optional<std::chrono::seconds> duration;
    std::filesystem::path viiperExecutable;
    asb::dualsense::VirtualDualSenseBackend virtualBackend =
        asb::dualsense::VirtualDualSenseBackend::Auto;
    bool proxyXInput = true;
    bool routeRumble = false;
    bool verifyVirtualInput = false;
    bool isolateApex = true;
    asb::dualsense::TouchpadGestureProfile touchpadProfile =
        asb::dualsense::TouchpadGestureProfile::None;
    bool touchpadProfileExplicit = false;
    unsigned int hapticThresholdPercent = 12;
    bool hapticThresholdExplicit = false;
    std::optional<unsigned int> xinputIndex;
    std::optional<std::string> sessionToken;
    std::filesystem::path telemetryJson;
};

bool parseBridgeOptions(int argc, char** argv, BridgeCommandOptions& options,
                        std::string& error) {
    for (int i = 2; i < argc; ++i) {
        const std::string_view value = argv[i];
        if (value == "--seconds") {
            if (++i >= argc) { error = "--seconds requires an integer from 1 to 86400."; return false; }
            try {
                const auto seconds = std::stoul(argv[i]);
                if (seconds == 0 || seconds > 86400) throw std::out_of_range("seconds");
                options.duration = std::chrono::seconds(seconds);
            } catch (...) { error = "--seconds requires an integer from 1 to 86400."; return false; }
        } else if (value == "--viiper") {
            if (++i >= argc) { error = "--viiper requires a path."; return false; }
            options.viiperExecutable = argv[i];
        } else if (value == "--virtual-backend") {
            if (++i >= argc) {
                error = "--virtual-backend requires auto, integrated, or sidecar.";
                return false;
            }
            const auto backend = parseVirtualDualSenseBackend(argv[i]);
            if (!backend) {
                error = "--virtual-backend requires auto, integrated, or sidecar.";
                return false;
            }
            options.virtualBackend = *backend;
        } else if (value == "--telemetry-json") {
            if (++i >= argc) { error = "--telemetry-json requires a file path."; return false; }
            options.telemetryJson = argv[i];
        } else if (value == "--proxy-xinput") {
            options.proxyXInput = true;
        } else if (value == "--rumble") {
            options.routeRumble = true;
        } else if (value == "--haptic-threshold") {
            if (++i >= argc) {
                error = "--haptic-threshold requires an integer percentage from 0 to 95.";
                return false;
            }
            try {
                std::size_t parsedCharacters = 0;
                const auto parsed = std::stoul(argv[i], &parsedCharacters);
                if (parsedCharacters != std::string_view(argv[i]).size() || parsed > 95) {
                    throw std::out_of_range("haptic-threshold");
                }
                options.hapticThresholdPercent = static_cast<unsigned int>(parsed);
                options.hapticThresholdExplicit = true;
            } catch (...) {
                error = "--haptic-threshold requires an integer percentage from 0 to 95.";
                return false;
            }
        } else if (value == "--verify-virtual-input") {
            options.verifyVirtualInput = true;
            options.proxyXInput = true;
        } else if (value == "--touchpad-profile") {
            if (++i >= argc) {
                error = "--touchpad-profile requires one of: none, spider-man-2, miles-morales, ghost-of-tsushima, warframe.";
                return false;
            }
            const auto profile = asb::dualsense::parseTouchpadGestureProfile(argv[i]);
            if (!profile || *profile == asb::dualsense::TouchpadGestureProfile::LegacyViewHoldSwipeUp) {
                error = "Unknown --touchpad-profile. Expected none, spider-man-2, miles-morales, ghost-of-tsushima, or warframe.";
                return false;
            }
            options.touchpadProfile = *profile;
            options.touchpadProfileExplicit = true;
            options.proxyXInput = true;
        } else if (value == "--view-hold-swipe-up") {
            options.touchpadProfile =
                asb::dualsense::TouchpadGestureProfile::LegacyViewHoldSwipeUp;
            options.touchpadProfileExplicit = true;
            options.proxyXInput = true;
        } else if (value == "--isolate-apex") {
            options.isolateApex = true;
            options.proxyXInput = true;
        } else if (value == "--xinput-index") {
            if (++i >= argc) { error = "--xinput-index requires a value from 0 to 3."; return false; }
            try {
                const auto parsed = std::stoul(argv[i]);
                if (parsed > 3) throw std::out_of_range("xinput-index");
                options.xinputIndex = static_cast<unsigned int>(parsed);
                options.proxyXInput = true;
            } catch (...) { error = "--xinput-index requires a value from 0 to 3."; return false; }
        } else if (value == "--session-token") {
            if (++i >= argc) {
                error = "--session-token requires exactly 32 hexadecimal characters.";
                return false;
            }
            const std::string token = argv[i];
            if (!asb::platform::isValidSessionToken(token)) {
                error = "--session-token requires exactly 32 hexadecimal characters.";
                return false;
            }
            options.sessionToken = token;
        } else if (!value.empty() && value.front() != '-' && !options.deviceIndex) {
            try { options.deviceIndex = static_cast<std::size_t>(std::stoul(std::string(value))); }
            catch (...) { error = "The device index must be an integer."; return false; }
        } else {
            error = "Unknown bridge-triggers option: " + std::string(value);
            return false;
        }
    }
    if (options.hapticThresholdExplicit && !options.routeRumble) {
        error = "--haptic-threshold requires --rumble.";
        return false;
    }
    // A DualSense session is always a complete physical-input proxy. These
    // invariants are enforced by the engine, not merely by the Playnite UI.
    options.proxyXInput = true;
    options.isolateApex = true;
    return true;
}

int commandBridgeTriggers(int argc, char** argv) {
    const auto initializationStartedAt = std::chrono::steady_clock::now();
    g_stopRequested.store(false, std::memory_order_relaxed);
    BridgeCommandOptions options{};
    std::string error;
    if (!parseBridgeOptions(argc, argv, options, error)) {
        std::cerr << error << "\nUsage: ApexSenseBridge bridge-triggers [index] [--seconds N] [--viiper PATH] [--virtual-backend auto|integrated|sidecar] [--telemetry-json PATH] [--proxy-xinput] [--xinput-index 0..3] [--rumble] [--haptic-threshold 0..95] [--verify-virtual-input] [--touchpad-profile NAME] [--view-hold-swipe-up] [--isolate-apex] [--session-token 32HEX]\n";
        return 1;
    }

    auto globalSessionStop = asb::platform::createGlobalSessionStop(error);
    if (!globalSessionStop) {
        std::cerr << "Global maintenance stop initialization failed: " << error << '\n';
        return 13;
    }

    std::unique_ptr<asb::platform::SessionControl> sessionControl;
    if (options.sessionToken) {
        sessionControl = asb::platform::connectSessionControl(*options.sessionToken, error);
        if (!sessionControl) {
            std::cerr << "Playnite session IPC connection failed: " << error << '\n';
            return 13;
        }
        if (!sessionControl->publish(asb::platform::SessionPhase::Starting, 0,
                                     "Bridge initialization started.", error)) {
            std::string ignored;
            (void)sessionControl->signalReady(ignored);
            std::cerr << "Playnite session status initialization failed: " << error << '\n';
            return 13;
        }
    }
    const auto failSession = [&sessionControl](int exitCode, std::string_view message) {
        if (sessionControl) {
            std::string ignored;
            (void)sessionControl->publish(asb::platform::SessionPhase::Failed,
                                          exitCode, message, ignored);
            // Ready doubles as initialization-complete: on failure it wakes the
            // caller so it can read the status immediately instead of timing out.
            (void)sessionControl->signalReady(ignored);
        }
        return exitCode;
    };

    auto device = openSelectedIndex(options.deviceIndex, error);
    if (!device) {
        const std::string message = "APEX identity check failed: " + error;
        std::cerr << message << '\n';
        return failSession(3, message);
    }
    asb::TriggerResetGuard resetOnExit(*device);
    if (!device->clearAll(error)) {
        std::cerr << "Could not establish a Normal trigger baseline: " << error << '\n';
        return failSession(4, "Could not establish a Normal trigger baseline: " + error);
    }

    std::unique_ptr<asb::RumbleResetGuard> rumbleResetOnExit;
    if (options.routeRumble) {
        if (!device->stopRumble(error)) {
            std::cerr << "Could not establish a stopped grip-rumble baseline: "
                      << error << '\n';
            return failSession(12, "Could not establish a stopped grip-rumble baseline: " + error);
        }
        rumbleResetOnExit = std::make_unique<asb::RumbleResetGuard>(*device);
    }

    auto inputSource = asb::platform::openPhysicalInputSource(
        device->info(), options.xinputIndex, error);
    if (!inputSource) {
        std::cerr << "Mandatory physical-input proxy creation failed: " << error << '\n';
        return failSession(8, "Mandatory physical-input proxy creation failed: " + error);
    }
    const std::string inputBackend(inputSource->backendName());
    asb::dualsense::DualSenseInputState initialInput{};
    const auto initialStatus = inputSource->waitForState(
        initialInput,
        inputSource->eventDriven() ? std::chrono::milliseconds(1000)
                                   : std::chrono::milliseconds(1),
        error);
    if (initialStatus != asb::platform::PhysicalInputStatus::State) {
        const std::string message = error.empty()
            ? "The selected APEX produced no complete input state during initialization."
            : error;
        std::cerr << "Mandatory physical-input proxy validation failed: " << message << '\n';
        return failSession(8, "Mandatory physical-input proxy validation failed: " + message);
    }
    const auto physicalInputReadyAt = std::chrono::steady_clock::now();

    const auto preexistingDualSensePaths = snapshotDualSensePaths();
    asb::platform::VirtualDualSenseAudioEndpointProtection audioProtection;
    std::string audioProtectionError;
    if (!audioProtection.capture(audioProtectionError)) {
        std::cerr << "Warning: Windows default-audio protection is unavailable: "
                  << audioProtectionError << '\n';
    }

    asb::dualsense::AdaptiveTriggerBridge bridge(*device);
    asb::haptics::HapticConfig hapticConfig{};
    hapticConfig.activationThreshold =
        static_cast<double>(options.hapticThresholdPercent) / 100.0;
    auto rumbleBridge = options.routeRumble
        ? std::make_unique<asb::dualsense::RumbleBridge>(*device, hapticConfig)
        : std::unique_ptr<asb::dualsense::RumbleBridge>{};
    asb::dualsense::VirtualDualSenseOptions backendOptions{};
    backendOptions.viiperExecutable = std::move(options.viiperExecutable);
    backendOptions.backend = options.virtualBackend;
    auto virtualDualSense = asb::dualsense::createVirtualDualSense(std::move(backendOptions));
    if (!virtualDualSense->open(
            error,
            [&bridge, rumble = rumbleBridge.get()](const auto& feedback) {
                bridge.handle(feedback);
                if (rumble) rumble->handle(feedback);
            })) {
        std::cerr << "Virtual DualSense creation failed: " << error << '\n';
        return failSession(6, "Virtual DualSense creation failed: " + error);
    }

    // Validate the complete physical -> virtual translation before hiding the
    // original controller or allowing the game to start.
    if (!virtualDualSense->updateInput(initialInput, error)) {
        virtualDualSense->close();
        return failSession(8, "Initial physical-to-DualSense input forwarding failed: " + error);
    }
    const auto virtualInputReadyAt = std::chrono::steady_clock::now();

    // Endpoint publication can take two seconds even when Windows ultimately
    // leaves the default output unchanged. Observe it in parallel with the HID
    // readiness/isolation work so it no longer stalls Playnite's launch hook.
    auto audioProtectionFuture = std::async(
        std::launch::async,
        [&audioProtection, &audioProtectionError]() {
            return !audioProtection.captured() ||
                   audioProtection.protectAfterVirtualDualSenseStart(
                       std::chrono::milliseconds(2000), audioProtectionError);
        });

    std::string firmwareError;
    const auto virtualFirmware = readNewVirtualDualSenseFirmware(
        preexistingDualSensePaths, std::chrono::milliseconds(1500), firmwareError);
    if (!virtualFirmware) {
        std::cerr << "Warning: virtual DualSense firmware verification failed: "
                  << firmwareError << '\n';
    }
    const auto firmwareCheckedAt = std::chrono::steady_clock::now();

    using MonitorPtr = std::unique_ptr<asb::platform::HidTransport,
                                       void (*)(asb::platform::HidTransport*)>;
    MonitorPtr virtualInputMonitor(nullptr, asb::platform::destroyHidTransport);
    if (options.verifyVirtualInput) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline && !virtualInputMonitor) {
            std::string enumerateError;
            const auto devices = asb::platform::enumerateHidDevices(enumerateError);
            for (const auto& info : devices) {
                if (info.vendorId == 0x054C && info.productId == 0x0CE6 &&
                    info.usagePage == 0x0001 && info.usage == 0x0005 &&
                    info.inputReportLength >= 64) {
                    std::string openError;
                    virtualInputMonitor.reset(asb::platform::createHidTransport(info, openError));
                    if (!virtualInputMonitor) error = std::move(openError);
                    break;
                }
            }
            if (!virtualInputMonitor) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!virtualInputMonitor) {
            virtualDualSense->close();
            std::cerr << "Could not open the virtual DualSense HID input for verification: "
                      << (error.empty() ? "interface not found" : error) << '\n';
            return failSession(9, "Could not open the virtual DualSense HID input for verification: " +
                                      (error.empty() ? std::string("interface not found") : error));
        }
    }

    asb::platform::TemporaryPhysicalControllerIsolation physicalIsolation;
    if (!physicalIsolation.activate(
            device->info(), options.sessionToken.value_or(""), error)) {
        virtualDualSense->close();
        std::cerr << "Temporary APEX isolation failed: " << error << '\n';
        return failSession(11, "Temporary APEX isolation failed: " + error);
    }
    const auto isolationReadyAt = std::chrono::steady_clock::now();

    if (sessionControl) {
        if (!sessionControl->publish(asb::platform::SessionPhase::Ready, 0,
                                     "Bridge ready; game launch may continue.", error) ||
            !sessionControl->signalReady(error)) {
            virtualDualSense->close();
            std::string ignored;
            (void)physicalIsolation.restore(ignored);
            std::cerr << "Playnite session ready signal failed: " << error << '\n';
            return failSession(13, "Playnite session ready signal failed: " + error);
        }
    }

    const auto initializedAt = std::chrono::steady_clock::now();
    const auto initializationMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            initializedAt - initializationStartedAt).count();
    const auto physicalInputInitializationMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            physicalInputReadyAt - initializationStartedAt).count();
    const auto virtualInputInitializationMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            virtualInputReadyAt - physicalInputReadyAt).count();
    const auto firmwareInitializationMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            firmwareCheckedAt - virtualInputReadyAt).count();
    const auto isolationInitializationMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            isolationReadyAt - firmwareCheckedAt).count();
    const auto processUsageStarted = processUsageSnapshot();

    std::cout << "APEX verified: " << device->identity()->describe() << '\n'
              << "Adaptive-trigger routing enabled.\n"
              << (rumbleBridge
                      ? "Grip-rumble and DualSense audio-haptics routing enabled.\n"
                      : "Grip-rumble and audio haptics routing remain disabled.\n")
              << "All APEX controls are proxied through " << inputBackend
              << " into the virtual DualSense.\n"
              << "Virtual DualSense backend: "
              << virtualDualSense->stats().backendVersion << ".\n"
              << (virtualFirmware
                      ? "Virtual DualSense firmware " +
                            hex16(virtualFirmware->updateVersion) +
                            (virtualFirmware->updateVersion >= 0x0630
                                 ? " verified.\n"
                                 : " is obsolete; newer games may disable native feedback.\n")
                      : "")
              << (virtualInputMonitor ? "Virtual DualSense HID input verification is enabled.\n" : "")
              << (options.touchpadProfile != asb::dualsense::TouchpadGestureProfile::None
                      ? "Touchpad gesture profile: " +
                            std::string(asb::dualsense::touchpadGestureProfileName(
                                options.touchpadProfile)) + ".\n"
                      : "")
              << (physicalIsolation.active()
                      ? "The original APEX game interface is hidden for this bridge session only.\n"
                      : "")
              << (sessionControl ? "Playnite session IPC is ready.\n" : "")
              << (rumbleBridge
                      ? "Audio-haptics activation threshold: " +
                            std::to_string(options.hapticThresholdPercent) + "%\n"
                      : "")
              << (options.duration ? "Bridge running...\n" : "Bridge running; press Ctrl+C to stop cleanly.\n");
    const auto started = initializedAt;
    bool disconnected = false;
    bool inputProxyFailed = false;
    std::string inputProxyError;
    std::optional<asb::dualsense::DualSenseInputState> lastPhysicalInput = initialInput;
    std::optional<asb::dualsense::DualSenseInputState> lastForwardedInput = initialInput;
    std::uint64_t inputSamples = 1;
    std::uint64_t buttonTransitions = 0;
    std::uint16_t seenButtons = 0;
    std::uint8_t seenDpad = 0;
    std::uint8_t maximumL2 = 0;
    std::uint8_t maximumR2 = 0;
    std::uint8_t minimumRightStickX = initialInput.rx;
    std::uint8_t maximumRightStickX = initialInput.rx;
    std::uint8_t minimumRightStickY = initialInput.ry;
    std::uint8_t maximumRightStickY = initialInput.ry;
    std::uint64_t virtualInputReports = 0;
    std::uint8_t virtualSeenFace = 0;
    std::uint8_t virtualSeenShoulders = 0;
    std::uint8_t virtualSeenSystem = 0;
    std::uint16_t virtualSeenDpadHats = 0;
    std::uint8_t virtualMaximumL2 = 0;
    std::uint8_t virtualMaximumR2 = 0;
    std::uint8_t virtualMinimumRightStickX = 0xFF;
    std::uint8_t virtualMaximumRightStickX = 0;
    std::uint8_t virtualMinimumRightStickY = 0xFF;
    std::uint8_t virtualMaximumRightStickY = 0;
    std::uint64_t virtualTouchStarts = 0;
    std::uint64_t virtualTouchActiveReports = 0;
    std::uint64_t virtualTouchMovementReports = 0;
    std::uint64_t coalescedInputReports = 0;
    std::uint64_t keepaliveInputReports = 0;
    std::uint64_t forwardedPhysicalReports = 1;
    MicrosecondLatencyHistogram forwardingLatency;
    std::uint16_t virtualTouchMinimumX = 0xFFFF;
    std::uint16_t virtualTouchMaximumX = 0;
    std::uint16_t virtualTouchMinimumY = 0xFFFF;
    std::uint16_t virtualTouchMaximumY = 0;
    bool virtualTouchWasActive = false;
    std::uint16_t previousVirtualTouchX = 0;
    std::uint16_t previousVirtualTouchY = 0;
    std::vector<std::uint8_t> virtualInputBuffer(64, 0);
    ButtonHoldTracker mappedTouchpadHold;
    ButtonHoldTracker virtualTouchpadHold;
    asb::dualsense::TouchpadGestureMapper touchpadGestureMapper(
        options.touchpadProfile);
    auto lastInputForwardedAt = std::chrono::steady_clock::now();
    constexpr auto kInputKeepalive = std::chrono::milliseconds(100);
    while (!g_stopRequested.load(std::memory_order_relaxed) &&
           !globalSessionStop->stopRequested() &&
           (!sessionControl || !sessionControl->stopRequested()) &&
           !bridge.failed() && (!rumbleBridge || !rumbleBridge->failed())) {
        asb::dualsense::DualSenseInputState input{};
        const auto inputWait = inputSource->eventDriven()
            ? std::chrono::milliseconds(8)
            : std::chrono::milliseconds(1);
        const auto inputStatus = inputSource->waitForState(
            input, inputWait, inputProxyError);
        bool forwardInput = false;
        const auto inputObservedAt = std::chrono::steady_clock::now();
        if (inputStatus == asb::platform::PhysicalInputStatus::State) {
            ++inputSamples;
            seenButtons = static_cast<std::uint16_t>(seenButtons | input.buttons);
            seenDpad = static_cast<std::uint8_t>(seenDpad | input.dpad);
            if (input.l2 > maximumL2) maximumL2 = input.l2;
            if (input.r2 > maximumR2) maximumR2 = input.r2;
            minimumRightStickX = (std::min)(minimumRightStickX, input.rx);
            maximumRightStickX = (std::max)(maximumRightStickX, input.rx);
            minimumRightStickY = (std::min)(minimumRightStickY, input.ry);
            maximumRightStickY = (std::max)(maximumRightStickY, input.ry);
            if (lastPhysicalInput && lastPhysicalInput->buttons != input.buttons) {
                ++buttonTransitions;
            }
            lastPhysicalInput = input;
            mappedTouchpadHold.observe(
                (input.buttons & asb::dualsense::button::kTouchpadClick) != 0,
                inputObservedAt);
            if (options.touchpadProfile != asb::dualsense::TouchpadGestureProfile::None) {
                touchpadGestureMapper.transform(input, inputObservedAt);
            }
            // Event-driven HID reports are forwarded immediately. The XInput
            // fallback polls at 1 ms but coalesces unchanged states.
            forwardInput = inputSource->eventDriven() ||
                           !lastForwardedInput || *lastForwardedInput != input;
            if (!forwardInput) ++coalescedInputReports;
        } else if (inputStatus == asb::platform::PhysicalInputStatus::Timeout) {
            if (lastPhysicalInput) {
                input = *lastPhysicalInput;
                if (options.touchpadProfile != asb::dualsense::TouchpadGestureProfile::None) {
                    touchpadGestureMapper.transform(input, inputObservedAt);
                }
                forwardInput = !lastForwardedInput ||
                               *lastForwardedInput != input ||
                               inputObservedAt - lastInputForwardedAt >= kInputKeepalive;
            }
        } else {
            inputProxyFailed = true;
            if (inputProxyError.empty()) {
                inputProxyError = inputStatus == asb::platform::PhysicalInputStatus::Disconnected
                    ? "The mandatory physical APEX input source disconnected."
                    : "The mandatory physical APEX input source failed.";
            }
            break;
        }
        if (forwardInput) {
            if (!virtualDualSense->updateInput(input, inputProxyError)) {
                inputProxyFailed = true;
                break;
            }
            forwardingLatency.observe(std::chrono::steady_clock::now() - inputObservedAt);
            if (inputStatus == asb::platform::PhysicalInputStatus::State) {
                ++forwardedPhysicalReports;
            } else {
                ++keepaliveInputReports;
            }
            lastForwardedInput = input;
            lastInputForwardedAt = inputObservedAt;
        }
        if (virtualInputMonitor) {
            std::size_t bytesRead = 0;
            std::string readError;
            const auto readStatus = virtualInputMonitor->readInputReport(
                virtualInputBuffer, std::chrono::milliseconds(1), bytesRead, readError);
            if (readStatus == asb::platform::HidReadStatus::Error) {
                inputProxyFailed = true;
                inputProxyError = "Virtual DualSense HID verification failed: " + readError;
                break;
            }
            if (readStatus == asb::platform::HidReadStatus::Data &&
                bytesRead >= 11 && virtualInputBuffer[0] == 0x01) {
                ++virtualInputReports;
                const auto hat = static_cast<std::uint8_t>(virtualInputBuffer[8] & 0x0F);
                if (hat < 16) {
                    virtualSeenDpadHats = static_cast<std::uint16_t>(
                        virtualSeenDpadHats | (std::uint16_t{1} << hat));
                }
                virtualSeenFace = static_cast<std::uint8_t>(
                    virtualSeenFace | (virtualInputBuffer[8] & 0xF0));
                virtualSeenShoulders = static_cast<std::uint8_t>(
                    virtualSeenShoulders | virtualInputBuffer[9]);
                virtualSeenSystem = static_cast<std::uint8_t>(
                    virtualSeenSystem | virtualInputBuffer[10]);
                virtualTouchpadHold.observe(
                    (virtualInputBuffer[10] & 0x02) != 0,
                    std::chrono::steady_clock::now());
                if (bytesRead >= 37) {
                    const bool touchActive = (virtualInputBuffer[33] & 0x80) == 0;
                    if (touchActive) {
                        const auto touchX = static_cast<std::uint16_t>(
                            virtualInputBuffer[34] |
                            ((virtualInputBuffer[35] & 0x0F) << 8));
                        const auto touchY = static_cast<std::uint16_t>(
                            (virtualInputBuffer[35] >> 4) |
                            (virtualInputBuffer[36] << 4));
                        ++virtualTouchActiveReports;
                        if (!virtualTouchWasActive) ++virtualTouchStarts;
                        if (virtualTouchWasActive &&
                            (touchX != previousVirtualTouchX ||
                             touchY != previousVirtualTouchY)) {
                            ++virtualTouchMovementReports;
                        }
                        virtualTouchMinimumX = (std::min)(virtualTouchMinimumX, touchX);
                        virtualTouchMaximumX = (std::max)(virtualTouchMaximumX, touchX);
                        virtualTouchMinimumY = (std::min)(virtualTouchMinimumY, touchY);
                        virtualTouchMaximumY = (std::max)(virtualTouchMaximumY, touchY);
                        previousVirtualTouchX = touchX;
                        previousVirtualTouchY = touchY;
                    }
                    virtualTouchWasActive = touchActive;
                }
                if (virtualInputBuffer[5] > virtualMaximumL2) virtualMaximumL2 = virtualInputBuffer[5];
                if (virtualInputBuffer[6] > virtualMaximumR2) virtualMaximumR2 = virtualInputBuffer[6];
                virtualMinimumRightStickX =
                    (std::min)(virtualMinimumRightStickX, virtualInputBuffer[3]);
                virtualMaximumRightStickX =
                    (std::max)(virtualMaximumRightStickX, virtualInputBuffer[3]);
                virtualMinimumRightStickY =
                    (std::min)(virtualMinimumRightStickY, virtualInputBuffer[4]);
                virtualMaximumRightStickY =
                    (std::max)(virtualMaximumRightStickY, virtualInputBuffer[4]);
            }
        }
        if (!virtualDualSense->connected()) { disconnected = true; break; }
        if (options.duration && std::chrono::steady_clock::now() - started >= *options.duration) break;
    }
    const auto runtimeMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    const auto trackingFinishedAt = std::chrono::steady_clock::now();
    mappedTouchpadHold.finish(trackingFinishedAt);
    virtualTouchpadHold.finish(trackingFinishedAt);
    const bool audioProtectionOk = audioProtectionFuture.get();
    if (!audioProtectionOk) {
        std::cerr << "Warning: Windows default-audio protection failed: "
                  << audioProtectionError << '\n';
    }

    if (sessionControl) {
        std::string ignored;
        (void)sessionControl->publish(asb::platform::SessionPhase::Stopping, 0,
                                      "Bridge cleanup in progress.", ignored);
    }
    // Playnite Fullscreen regains focus as soon as the game stops. Clear the
    // last forwarded button state before detaching the virtual controller so a
    // held Cross/A press cannot become a new launch command in Playnite.
    std::string neutralizationError;
    const bool virtualInputNeutralized = virtualDualSense->updateInput(
        asb::dualsense::DualSenseInputState{}, neutralizationError);
    if (virtualInputNeutralized) {
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
    } else {
        std::cerr << "Warning: virtual input neutralization during cleanup failed: "
                  << neutralizationError << '\n';
    }
    virtualDualSense->close(); // joins the feedback callback before touching the HID device
    const auto virtualStats = virtualDualSense->stats();
    const auto touchpadGestureStats = touchpadGestureMapper.stats();
    const auto bridgeStats = bridge.stats();
    const auto rumbleStats = rumbleBridge
        ? rumbleBridge->stats()
        : asb::dualsense::RumbleBridgeStats{};
    std::string rumbleResetError;
    const bool rumbleResetOk = !rumbleBridge || device->stopRumble(rumbleResetError);
    if (rumbleResetOk && rumbleResetOnExit) rumbleResetOnExit->dismiss();
    std::string resetError;
    const bool resetOk = device->clearAll(resetError);
    if (resetOk) resetOnExit.dismiss();
    // Keep HidHide active until launch-capable controls have been released for
    // a short stable interval. The wait is bounded so disconnects and damaged
    // devices can never prevent restoration/uninstall.
    const bool physicalControlsReleased = waitForPhysicalControlsReleased(
        *inputSource, std::chrono::milliseconds(1500));
    std::string isolationRestoreError;
    const bool isolationRestored = physicalIsolation.restore(isolationRestoreError);
    const auto inputSourceStats = inputSource->stats();
    const auto processUsageFinished = processUsageSnapshot();
    const double runtimeSeconds = runtimeMilliseconds > 0
        ? static_cast<double>(runtimeMilliseconds) / 1000.0
        : 0.0;
    const double physicalReportRateHz = runtimeSeconds > 0.0
        ? static_cast<double>(inputSourceStats.reports) / runtimeSeconds
        : 0.0;
    // The backend setter only runs when the physical state changes, while the
    // virtual USB controller keeps emitting complete HID reports. When the
    // verification monitor is enabled, report the observed HID cadence rather
    // than the (usually much lower) state-update cadence.
    const std::uint64_t measuredVirtualReports = virtualInputMonitor
        ? virtualInputReports
        : virtualStats.inputUpdates;
    const double virtualReportRateHz = runtimeSeconds > 0.0
        ? static_cast<double>(measuredVirtualReports) / runtimeSeconds
        : 0.0;
    const auto cpuDelta100ns = processUsageFinished.cpu100ns >= processUsageStarted.cpu100ns
        ? processUsageFinished.cpu100ns - processUsageStarted.cpu100ns
        : 0;
    const double cpuPercent = runtimeSeconds > 0.0
        ? (static_cast<double>(cpuDelta100ns) / 10000000.0) /
              runtimeSeconds / static_cast<double>(logicalProcessorCount()) * 100.0
        : 0.0;
    const auto lostInputReports = inputSourceStats.parseFailures +
        static_cast<std::uint64_t>(inputProxyFailed ? 1 : 0);

    if (!options.telemetryJson.empty()) {
        std::ofstream telemetry(options.telemetryJson, std::ios::binary | std::ios::trunc);
        if (!telemetry) {
            std::cerr << "Warning: could not create telemetry JSON file: "
                      << options.telemetryJson.string() << '\n';
        } else {
            telemetry << std::fixed << std::setprecision(3)
                      << "{\n"
                      << "  \"schema\": 1,\n"
                      << "  \"virtual_backend\": \""
                      << jsonEscape(virtualStats.backendVersion) << "\",\n"
                      << "  \"input_mode\": \"mandatory-full-proxy\",\n"
                      << "  \"input_backend\": \"" << jsonEscape(inputBackend) << "\",\n"
                      << "  \"initialization_ms\": " << initializationMilliseconds << ",\n"
                      << "  \"initialization_physical_input_ms\": "
                      << physicalInputInitializationMilliseconds << ",\n"
                      << "  \"initialization_virtual_input_ms\": "
                      << virtualInputInitializationMilliseconds << ",\n"
                      << "  \"initialization_firmware_ms\": "
                      << firmwareInitializationMilliseconds << ",\n"
                      << "  \"initialization_isolation_ms\": "
                      << isolationInitializationMilliseconds << ",\n"
                      << "  \"backend_initialization_bootstrap_us\": "
                      << virtualStats.initializationBootstrapUs << ",\n"
                      << "  \"backend_initialization_server_us\": "
                      << virtualStats.initializationServerUs << ",\n"
                      << "  \"backend_initialization_bus_us\": "
                      << virtualStats.initializationBusUs << ",\n"
                      << "  \"backend_initialization_device_us\": "
                      << virtualStats.initializationDeviceUs << ",\n"
                      << "  \"backend_initialization_feedback_us\": "
                      << virtualStats.initializationFeedbackUs << ",\n"
                      << "  \"backend_initialization_input_us\": "
                      << virtualStats.initializationInputUs << ",\n"
                      << "  \"runtime_ms\": " << runtimeMilliseconds << ",\n"
                      << "  \"forward_latency_us_p50\": " << forwardingLatency.percentile(50) << ",\n"
                      << "  \"forward_latency_us_p95\": " << forwardingLatency.percentile(95) << ",\n"
                      << "  \"forward_latency_us_p99\": " << forwardingLatency.percentile(99) << ",\n"
                      << "  \"forward_latency_samples\": " << forwardingLatency.samples() << ",\n"
                      << "  \"physical_report_rate_hz\": " << physicalReportRateHz << ",\n"
                      << "  \"virtual_report_rate_hz\": " << virtualReportRateHz << ",\n"
                      << "  \"physical_reports\": " << inputSourceStats.reports << ",\n"
                      << "  \"forwarded_physical_reports\": " << forwardedPhysicalReports << ",\n"
                      << "  \"keepalive_reports\": " << keepaliveInputReports << ",\n"
                      << "  \"lost_reports\": " << lostInputReports << ",\n"
                      << "  \"coalesced_reports\": " << coalescedInputReports << ",\n"
                      << "  \"cpu_percent_total\": " << cpuPercent << ",\n"
                      << "  \"working_set_mib\": "
                      << static_cast<double>(processUsageFinished.workingSetBytes) / (1024.0 * 1024.0) << ",\n"
                      << "  \"peak_working_set_mib\": "
                      << static_cast<double>(processUsageFinished.peakWorkingSetBytes) / (1024.0 * 1024.0) << ",\n"
                      << "  \"audio_haptics_received\": " << virtualStats.audioHapticsFrames << ",\n"
                      << "  \"audio_haptics_delivered\": " << virtualStats.audioHapticsDelivered << ",\n"
                      << "  \"audio_haptics_coalesced\": " << virtualStats.audioHapticsCoalesced << "\n"
                      << "}\n";
        }
    }

    std::cout << "apex_routing=adaptive-triggers\n"
              << "virtual_backend=" << virtualStats.backendVersion << '\n'
              << "input_mode=mandatory-full-proxy\n"
              << "input_backend=" << inputBackend << '\n'
              << "input_event_driven=" << (inputSource->eventDriven() ? "yes" : "no") << '\n'
              << "runtime_ms=" << runtimeMilliseconds << '\n'
              << "initialization_ms=" << initializationMilliseconds << '\n'
              << "initialization_physical_input_ms="
              << physicalInputInitializationMilliseconds << '\n'
              << "initialization_virtual_input_ms="
              << virtualInputInitializationMilliseconds << '\n'
              << "initialization_firmware_ms="
              << firmwareInitializationMilliseconds << '\n'
              << "initialization_isolation_ms="
              << isolationInitializationMilliseconds << '\n'
              << "backend_initialization_bootstrap_us="
              << virtualStats.initializationBootstrapUs << '\n'
              << "backend_initialization_server_us="
              << virtualStats.initializationServerUs << '\n'
              << "backend_initialization_bus_us="
              << virtualStats.initializationBusUs << '\n'
              << "backend_initialization_device_us="
              << virtualStats.initializationDeviceUs << '\n'
              << "backend_initialization_feedback_us="
              << virtualStats.initializationFeedbackUs << '\n'
              << "backend_initialization_input_us="
              << virtualStats.initializationInputUs << '\n'
              << "forward_latency_us_p50=" << forwardingLatency.percentile(50) << '\n'
              << "forward_latency_us_p95=" << forwardingLatency.percentile(95) << '\n'
              << "forward_latency_us_p99=" << forwardingLatency.percentile(99) << '\n'
              << "physical_report_rate_hz=" << std::fixed << std::setprecision(2)
              << physicalReportRateHz << '\n'
              << "virtual_report_rate_hz=" << virtualReportRateHz << '\n'
              << "cpu_percent_total=" << cpuPercent << std::defaultfloat << '\n'
              << "working_set_bytes=" << processUsageFinished.workingSetBytes << '\n'
              << "peak_working_set_bytes=" << processUsageFinished.peakWorkingSetBytes << '\n'
              << "input_reports_lost=" << lostInputReports << '\n'
              << "input_reports_coalesced=" << coalescedInputReports << '\n'
              << "input_keepalives=" << keepaliveInputReports << '\n'
              << "virtual_input_neutralized="
              << (virtualInputNeutralized ? "yes" : "no") << '\n'
              << "physical_controls_released_before_restore="
              << (physicalControlsReleased ? "yes" : "no") << '\n'
              << "input_updates=" << virtualStats.inputUpdates << '\n'
              << "dualsense_firmware_update="
              << (virtualFirmware ? hex16(virtualFirmware->updateVersion) : "unavailable")
              << '\n'
              << "dualsense_firmware_current="
              << (virtualFirmware && virtualFirmware->updateVersion >= 0x0630 ? "yes" : "no")
              << '\n'
              << "dualsense_output_reports=" << virtualStats.outputReports << '\n'
              << "dualsense_rumble_reports=" << virtualStats.rumbleReports << '\n'
              << "audio_haptics_frames=" << virtualStats.audioHapticsFrames << '\n'
              << "audio_haptics_delivered=" << virtualStats.audioHapticsDelivered << '\n'
              << "audio_haptics_coalesced=" << virtualStats.audioHapticsCoalesced << '\n'
              << "audio_default_protection="
              << asb::platform::audioDefaultProtectionStatusName(audioProtection.status())
              << '\n'
              << "audio_default_roles_restored=" << audioProtection.restoredRoles() << '\n'
              << "input_samples=" << inputSamples << '\n'
              << "button_transitions=" << buttonTransitions << '\n'
              << "touchpad_click_presses=" << mappedTouchpadHold.presses() << '\n'
              << "maximum_touchpad_click_hold_ms="
              << mappedTouchpadHold.maximumHoldMilliseconds() << '\n'
              << "touchpad_gesture_profile="
              << asb::dualsense::touchpadGestureProfileName(options.touchpadProfile)
              << '\n'
              << "view_touchpad_gesture="
              << (options.touchpadProfile != asb::dualsense::TouchpadGestureProfile::None
                      ? "enabled" : "disabled") << '\n'
              << "view_touchpad_taps=" << touchpadGestureStats.replayedTaps << '\n'
              << "view_touchpad_swipes=" << touchpadGestureStats.swipes << '\n'
              << "touchpad_swipes_up=" << touchpadGestureStats.swipesByDirection[0] << '\n'
              << "touchpad_swipes_down=" << touchpadGestureStats.swipesByDirection[1] << '\n'
              << "touchpad_swipes_left=" << touchpadGestureStats.swipesByDirection[2] << '\n'
              << "touchpad_swipes_right=" << touchpadGestureStats.swipesByDirection[3] << '\n'
              << "seen_buttons=0x" << std::hex << std::uppercase << std::setw(4)
              << std::setfill('0') << seenButtons << std::dec << std::setfill(' ') << '\n'
              << "seen_dpad=0x" << std::hex << std::uppercase
              << static_cast<unsigned>(seenDpad) << std::dec << '\n'
              << "maximum_l2=" << static_cast<unsigned>(maximumL2) << '\n'
              << "maximum_r2=" << static_cast<unsigned>(maximumR2) << '\n'
              << "right_stick_x_range="
              << static_cast<unsigned>(minimumRightStickX)
              << ',' << static_cast<unsigned>(maximumRightStickX) << '\n'
              << "right_stick_y_range="
              << static_cast<unsigned>(minimumRightStickY)
              << ',' << static_cast<unsigned>(maximumRightStickY) << '\n'
              << "virtual_input_reports=" << virtualInputReports << '\n'
              << "virtual_seen_face=0x" << std::hex << std::uppercase
              << static_cast<unsigned>(virtualSeenFace) << std::dec << '\n'
              << "virtual_seen_shoulders=0x" << std::hex << std::uppercase
              << static_cast<unsigned>(virtualSeenShoulders) << std::dec << '\n'
              << "virtual_seen_system=0x" << std::hex << std::uppercase
              << static_cast<unsigned>(virtualSeenSystem) << std::dec << '\n'
              << "virtual_touchpad_click_presses=" << virtualTouchpadHold.presses() << '\n'
              << "virtual_maximum_touchpad_click_hold_ms="
              << virtualTouchpadHold.maximumHoldMilliseconds() << '\n'
              << "virtual_touch_starts=" << virtualTouchStarts << '\n'
              << "virtual_touch_active_reports=" << virtualTouchActiveReports << '\n'
              << "virtual_touch_movement_reports=" << virtualTouchMovementReports << '\n'
              << "virtual_touch_minimum_x="
              << (virtualTouchActiveReports == 0 ? 0 : virtualTouchMinimumX) << '\n'
              << "virtual_touch_maximum_x=" << virtualTouchMaximumX << '\n'
              << "virtual_touch_minimum_y="
              << (virtualTouchActiveReports == 0 ? 0 : virtualTouchMinimumY) << '\n'
              << "virtual_touch_maximum_y=" << virtualTouchMaximumY << '\n'
              << "virtual_seen_dpad_hats=0x" << std::hex << std::uppercase
              << virtualSeenDpadHats << std::dec << '\n'
              << "virtual_maximum_l2=" << static_cast<unsigned>(virtualMaximumL2) << '\n'
              << "virtual_maximum_r2=" << static_cast<unsigned>(virtualMaximumR2) << '\n'
              << "virtual_right_stick_x_range="
              << (virtualInputReports == 0
                      ? 0 : static_cast<unsigned>(virtualMinimumRightStickX))
              << ',' << static_cast<unsigned>(virtualMaximumRightStickX) << '\n'
              << "virtual_right_stick_y_range="
              << (virtualInputReports == 0
                      ? 0 : static_cast<unsigned>(virtualMinimumRightStickY))
              << ',' << static_cast<unsigned>(virtualMaximumRightStickY) << '\n'
              << "translated_effects=" << bridgeStats.translated << '\n'
              << "active_effects=" << bridgeStats.active << '\n'
              << "normal_effects=" << bridgeStats.normal << '\n'
              << "deduplicated_effects=" << bridgeStats.deduplicated << '\n'
              << "neutral_requests=" << bridgeStats.neutral << '\n'
              << "unsupported_effects=" << bridgeStats.unsupported << '\n'
              << "write_failures=" << bridgeStats.writeFailures << '\n'
              << "rumble_routing=" << (rumbleBridge ? "enabled" : "disabled") << '\n'
              << "rumble_updates=" << rumbleStats.updates << '\n'
              << "rumble_writes=" << rumbleStats.writes << '\n'
              << "rumble_stops=" << rumbleStats.stops << '\n'
              << "rumble_deduplicated=" << rumbleStats.deduplicated << '\n'
              << "rumble_write_failures=" << rumbleStats.writeFailures << '\n'
              << "last_rumble_low=" << static_cast<unsigned>(rumbleStats.lastLowFrequency) << '\n'
              << "last_rumble_high=" << static_cast<unsigned>(rumbleStats.lastHighFrequency) << '\n'
              << "audio_haptics_routing=" << (rumbleBridge ? "enabled" : "disabled") << '\n'
              << "audio_haptics_processed=" << rumbleStats.audioFrames << '\n'
              << "audio_haptics_active=" << rumbleStats.audioActiveFrames << '\n'
              << "audio_haptics_active_percent=" << std::fixed << std::setprecision(2)
              << (rumbleStats.audioFrames == 0
                      ? 0.0
                      : 100.0 * static_cast<double>(rumbleStats.audioActiveFrames) /
                            static_cast<double>(rumbleStats.audioFrames))
              << std::defaultfloat << '\n'
              << "audio_haptics_rate_limited=" << rumbleStats.audioRateLimited << '\n'
              << "audio_haptics_timeouts=" << rumbleStats.audioTimeouts << '\n'
              << "audio_haptics_low_frames=" << rumbleStats.audioLowFrames << '\n'
              << "audio_haptics_medium_frames=" << rumbleStats.audioMediumFrames << '\n'
              << "audio_haptics_high_frames=" << rumbleStats.audioHighFrames << '\n'
              << "audio_haptics_threshold_percent=" << options.hapticThresholdPercent << '\n'
              << "audio_max_left_energy=" << rumbleStats.maximumLeftEnergy << '\n'
              << "audio_max_right_energy=" << rumbleStats.maximumRightEnergy << '\n'
              << "audio_max_left_peak=" << rumbleStats.maximumLeftPeak << '\n'
              << "audio_max_right_peak=" << rumbleStats.maximumRightPeak << '\n'
              << "audio_max_left_transient=" << rumbleStats.maximumLeftTransient << '\n'
              << "audio_max_right_transient=" << rumbleStats.maximumRightTransient << '\n'
              << "last_audio_low="
              << static_cast<unsigned>(rumbleStats.lastAudioLowFrequency) << '\n'
              << "last_audio_high="
              << static_cast<unsigned>(rumbleStats.lastAudioHighFrequency) << '\n';
    std::cout << "apex_original_restored="
              << (isolationRestored ? "yes" : "no") << '\n';
    const auto printLast = [](std::string_view side, std::uint8_t dsType,
                              const std::optional<asb::ForceTriggerCommand>& command) {
        std::cout << "last_" << side << "_ds_type=" << static_cast<unsigned>(dsType) << '\n';
        if (!command) {
            std::cout << "last_" << side << "_apex=none\n";
            return;
        }
        std::cout << "last_" << side << "_apex="
                  << static_cast<unsigned>(command->mode);
        for (const auto byte : command->params) std::cout << ',' << static_cast<unsigned>(byte);
        std::cout << '\n';
    };
    printLast("lt", bridgeStats.lastLeftDualSenseType, bridgeStats.lastLeftCommand);
    printLast("rt", bridgeStats.lastRightDualSenseType, bridgeStats.lastRightCommand);
    if (!resetOk) {
        std::cerr << "WARNING: LT/RT automatic reset failed: " << resetError
                  << "\nSet both triggers to Normal in Flydigi Space Station.\n";
        return failSession(5, "LT/RT automatic reset failed: " + resetError);
    }
    if (!rumbleResetOk) {
        std::cerr << "WARNING: grip-rumble automatic stop failed: "
                  << rumbleResetError << "\nPower-cycle the controller before continuing.\n";
        return failSession(12, "Grip-rumble automatic stop failed: " + rumbleResetError);
    }
    if (!isolationRestored) {
        std::cerr << "WARNING: could not restore the original APEX visibility: "
                  << isolationRestoreError
                  << "\nRun 'ApexSenseBridge restore-controller-visibility' before playing without the bridge.\n";
        return failSession(11, "Could not restore the original APEX visibility: " +
                                   isolationRestoreError);
    }
    if (bridge.failed()) {
        const std::string message = "Bridge stopped after an APEX write failure: " + bridge.error();
        std::cerr << message << '\n';
        return failSession(4, message);
    }
    if (rumbleBridge && rumbleBridge->failed()) {
        std::cerr << "Bridge stopped after an APEX rumble write failure: "
                  << rumbleBridge->error() << '\n';
        return failSession(12, "Bridge stopped after an APEX rumble write failure: " +
                                   rumbleBridge->error());
    }
    if (inputProxyFailed) {
        const std::string message =
            "Bridge stopped after a mandatory physical-input proxy failure: " +
            inputProxyError;
        std::cerr << message << '\n';
        return failSession(8, message);
    }
    if (disconnected) {
        constexpr std::string_view message = "The VIIPER feedback stream disconnected unexpectedly.";
        std::cerr << message << '\n';
        return failSession(7, message);
    }
    if (sessionControl &&
        !sessionControl->publish(asb::platform::SessionPhase::Stopped, 0,
                                 "Bridge stopped and controller state restored.", error)) {
        std::cerr << "Playnite session completion status failed: " << error << '\n';
        return 13;
    }
    std::cout << "LT and RT reset to Normal; grip rumble stopped.\n";
    return 0;
}

int commandList() {
    std::string error;
    const auto candidates = asb::flydigi::Apex5Device::findCandidates(error);
    if (!error.empty()) {
        std::cerr << "HID enumeration warning: " << error << "\n";
    }
    if (candidates.empty()) {
        std::cout << "No APEX 5 vendor HID interface found.\n";
        return 2;
    }
    std::cout << "Found " << candidates.size() << " candidate(s):\n\n";
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        printDevice(candidates[i], i);
    }
    return 0;
}

int commandDryRun() {
    asb::TriggerEffect effect{};
    effect.side = asb::TriggerSide::Right;
    effect.mode = asb::TriggerMode::Race;
    effect.start = 70;
    effect.p1 = 30;
    effect.matchInput = false;

    const auto report = asb::flydigi::buildForceTrigger(effect);
    for (std::size_t i = 0; i < report.size(); ++i) {
        if (i != 0 && i % 16 == 0) {
            std::cout << '\n';
        }
        std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                  << static_cast<int>(report[i]) << ' ';
    }
    std::cout << std::dec << "\n";
    return 0;
}

int commandIdentify(int argc, char** argv) {
    std::string error;
    auto device = openSelected(argc, argv, error);
    if (!device) {
        std::cerr << "Identity check failed: " << error << '\n';
        return 3;
    }

    const auto& identity = device->identity();
    std::cout << "Verified: " << identity->describe() << '\n'
              << "Connection: " << (identity->isWired() ? "wired" : "dongle")
              << " (raw " << static_cast<unsigned int>(identity->connectionTypeRaw()) << ")\n"
              << "Battery level: " << static_cast<unsigned int>(identity->batteryLevel())
              << (identity->isCharging() ? " (charging)" : "") << '\n'
              << "Adaptive triggers: yes\n";
    return 0;
}

int commandClear(int argc, char** argv) {
    std::string error;
    auto device = openSelected(argc, argv, error);
    if (!device) {
        std::cerr << "Error: " << error << "\n";
        return 3;
    }
    std::string triggerError;
    std::string rumbleError;
    const bool triggersCleared = device->clearAll(triggerError);
    const bool rumbleStopped = device->stopRumble(rumbleError);
    if (!triggersCleared || !rumbleStopped) {
        std::cerr << "Error while clearing APEX effects:";
        if (!triggersCleared) std::cerr << " triggers=" << triggerError;
        if (!rumbleStopped) std::cerr << " rumble=" << rumbleError;
        std::cerr << '\n';
        return 4;
    }
    std::cout << "LT and RT reset to Normal; grip rumble stopped.\n";
    return 0;
}

int commandTestRt(int argc, char** argv) {
    std::string error;
    auto device = openSelected(argc, argv, error);
    if (!device) {
        std::cerr << "Error: " << error << "\n";
        return 3;
    }

    std::cout << "Using: " << narrowAscii(device->info().product) << " ("
              << hex16(device->info().vendorId) << ':' << hex16(device->info().productId) << ")\n";
    std::cout << "Applying a GENTLE RT resistance for about 1.5 seconds...\n";

    asb::TriggerResetGuard resetOnExit(*device);

    // Exercise the same DualSense -> raw FORCEADAPT path as bridge-triggers.
    std::array<std::uint8_t, 11> dualSenseEffect{};
    dualSenseEffect[0] = 1;  // DualSense feedback/resistance
    dualSenseEffect[1] = 70; // start
    dualSenseEffect[2] = 30; // intentionally gentle resistance
    const auto translated = asb::dualsense::translateAdaptiveTrigger(
        asb::TriggerSide::Right, dualSenseEffect, 0);
    if (!translated || !device->setTriggerRaw(*translated, error)) {
        std::cerr << "Write failed: " << error << "\n";
        return 4;
    }

    constexpr auto duration = std::chrono::milliseconds(1500);
    constexpr auto slice = std::chrono::milliseconds(25);
    auto elapsed = std::chrono::milliseconds::zero();
    while (elapsed < duration && !g_stopRequested.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(slice);
        elapsed += slice;
    }

    if (!device->clearAll(error)) {
        std::cerr << "WARNING: automatic reset write failed: " << error << "\n"
                  << "Open Flydigi Space Station and set both triggers to Normal before continuing.\n";
        return 5;
    }
    resetOnExit.dismiss();

    std::cout << "RT reset to Normal.\n"
              << "If you felt a resistance begin part-way through RT, the Windows -> APEX FORCEADAPT path works.\n";
    return 0;
}

int commandTestRumble(int argc, char** argv) {
    std::string error;
    auto device = openSelected(argc, argv, error);
    if (!device) {
        std::cerr << "Error: " << error << "\n";
        return 3;
    }

    std::cout << "Using: " << narrowAscii(device->info().product) << " ("
              << hex16(device->info().vendorId) << ':' << hex16(device->info().productId) << ")\n";

    if (!device->stopRumble(error)) {
        std::cerr << "Could not establish a stopped rumble baseline: " << error << '\n';
        return 12;
    }
    asb::RumbleResetGuard resetOnExit(*device);

    std::cout << "Applying a GENTLE low/high-frequency grip vibration for about 1 second...\n";
    constexpr std::uint8_t kGentleLowFrequency = 48;
    constexpr std::uint8_t kGentleHighFrequency = 32;
    if (!device->setRumble(kGentleLowFrequency, kGentleHighFrequency, error)) {
        std::cerr << "Rumble write failed: " << error << '\n';
        return 12;
    }

    constexpr auto duration = std::chrono::milliseconds(1000);
    constexpr auto slice = std::chrono::milliseconds(20);
    auto elapsed = std::chrono::milliseconds::zero();
    while (elapsed < duration && !g_stopRequested.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(slice);
        elapsed += slice;
    }

    if (!device->stopRumble(error)) {
        std::cerr << "WARNING: automatic rumble stop failed: " << error
                  << "\nPower-cycle the controller before continuing.\n";
        return 12;
    }
    resetOnExit.dismiss();
    std::cout << "Grip rumble stopped. If both handles vibrated gently, command 0x12 works.\n";
    return 0;
}

int commandXInputViewTest(int argc, char** argv) {
    g_stopRequested.store(false, std::memory_order_relaxed);
    std::optional<unsigned int> requestedIndex;
    unsigned int seconds = 10;
    for (int index = 2; index < argc; ++index) {
        const std::string_view value = argv[index];
        if (value == "--seconds") {
            if (++index >= argc) {
                std::cerr << "--seconds requires an integer from 1 to 60.\n";
                return 1;
            }
            try {
                const auto parsed = std::stoul(argv[index]);
                if (parsed < 1 || parsed > 60) throw std::out_of_range("seconds");
                seconds = static_cast<unsigned int>(parsed);
            } catch (...) {
                std::cerr << "--seconds requires an integer from 1 to 60.\n";
                return 1;
            }
        } else if (!requestedIndex) {
            try {
                const auto parsed = std::stoul(std::string(value));
                if (parsed > 3) throw std::out_of_range("index");
                requestedIndex = static_cast<unsigned int>(parsed);
            } catch (...) {
                std::cerr << "XInput index must be 0, 1, 2, or 3.\n";
                return 1;
            }
        } else {
            std::cerr << "Unknown xinput-view-test option: " << value << '\n';
            return 1;
        }
    }

    std::string error;
    auto gamepad = asb::platform::openXInputGamepad(requestedIndex, error);
    if (!gamepad) {
        std::cerr << "XInput test could not start: " << error << '\n';
        return 8;
    }

    std::cout << "Testing XInput controller " << gamepad->index() << " for " << seconds
              << " seconds. Hold View/Back for at least 2 seconds now.\n";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    std::optional<std::chrono::steady_clock::time_point> pressedAt;
    std::chrono::milliseconds maximumHold{};
    std::uint64_t presses = 0;

    while (!g_stopRequested.load(std::memory_order_relaxed) &&
           std::chrono::steady_clock::now() < deadline) {
        asb::dualsense::DualSenseInputState input{};
        if (!gamepad->poll(input, error)) {
            std::cerr << "XInput test failed: " << error << '\n';
            return 8;
        }
        const auto now = std::chrono::steady_clock::now();
        const bool pressed =
            (input.buttons & asb::dualsense::button::kTouchpadClick) != 0;
        if (pressed && !pressedAt) {
            pressedAt = now;
            ++presses;
            std::cout << "view=pressed\n";
        } else if (!pressed && pressedAt) {
            const auto held = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - *pressedAt);
            if (held > maximumHold) maximumHold = held;
            std::cout << "view=released hold_ms=" << held.count() << '\n';
            pressedAt.reset();
        } else if (pressedAt) {
            const auto held = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - *pressedAt);
            if (held > maximumHold) maximumHold = held;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "view_presses=" << presses << '\n'
              << "maximum_view_hold_ms=" << maximumHold.count() << '\n'
              << "long_hold_seen="
              << (maximumHold >= std::chrono::milliseconds(1500) ? "yes" : "no")
              << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#endif

    if (argc < 2) {
        printUsage();
        return 0;
    }

    const std::string_view command = argv[1];
    if (command == "xinput-view-test") {
        return commandXInputViewTest(argc, argv);
    }
    if (command == "xinput-status") {
        const auto indices = asb::platform::connectedXInputGamepads();
        std::cout << "connected_xinput=";
        if (indices.empty()) {
            std::cout << "none";
        } else {
            for (std::size_t index = 0; index < indices.size(); ++index) {
                if (index != 0) std::cout << ',';
                std::cout << indices[index];
            }
        }
        std::cout << '\n';
        return 0;
    }
    if (command == "restore-controller-visibility") {
        bool controllerRecovered = false;
        std::string controllerError;
        const bool controllerOk =
            asb::platform::TemporaryPhysicalControllerIsolation::recoverPending(
                controllerRecovered, controllerError);
        if (!controllerOk) {
            std::cerr << "Controller visibility recovery failed: "
                      << controllerError << '\n';
            return 11;
        }
        std::cout << (controllerRecovered
                          ? "The original controller visibility was restored.\n"
                          : "No pending controller visibility recovery was found.\n");
        return 0;
    }
    if (command == "stop-active-sessions") {
        std::string error;
        if (!asb::platform::requestGlobalSessionStop(
                std::chrono::seconds(10), error)) {
            std::cerr << "Active bridge graceful stop failed: " << error << '\n';
            return 1;
        }
        std::cout << "No active bridge remains; virtual input and controller visibility cleanup completed.\n";
        return 0;
    }
    if (command == "hidhide-watchdog") {
        if (argc != 3 && argc != 4) return 1;
        try {
            const auto processId = std::stoul(argv[2]);
            if (processId == 0 || processId > 0xFFFFFFFFUL) return 1;
            const std::string_view sessionToken = argc == 4 ? argv[3] : "";
            std::string controllerError;
            const int controllerStatus =
                asb::platform::TemporaryPhysicalControllerIsolation::watchAndRecover(
                    static_cast<std::uint32_t>(processId), sessionToken,
                    controllerError);
            return controllerStatus == 0 ? 0 : 2;
        } catch (...) {
            return 1;
        }
    }
    if (command == "list") {
        return commandList();
    }
    if (command == "diagnose") {
        return commandDiagnose(argc, argv);
    }
    if (command == "input-status") {
        return commandInputStatus(argc, argv);
    }
    if (command == "identify") {
        return commandIdentify(argc, argv);
    }
    if (command == "virtual-ds") {
        return commandVirtualDs(argc, argv);
    }
    if (command == "bridge-triggers") {
        return commandBridgeTriggers(argc, argv);
    }
    if (command == "test-rt") {
        return commandTestRt(argc, argv);
    }
    if (command == "test-rumble") {
        return commandTestRumble(argc, argv);
    }
    if (command == "clear") {
        return commandClear(argc, argv);
    }
    if (command == "dry-run") {
        return commandDryRun();
    }

    printUsage();
    return 1;
}
