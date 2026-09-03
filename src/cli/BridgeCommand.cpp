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
namespace {

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

} // namespace

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
    printLast("active_lt", bridgeStats.lastActiveLeftDualSenseType,
              bridgeStats.lastActiveLeftCommand);
    printLast("active_rt", bridgeStats.lastActiveRightDualSenseType,
              bridgeStats.lastActiveRightCommand);
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

} // namespace asb::cli
