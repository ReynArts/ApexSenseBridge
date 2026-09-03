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

std::string dpadDescription(std::uint8_t dpad) {
    dpad &= 0x0F;
    if (dpad == 0) return "neutral";

    std::string description;
    const auto append = [&description](std::string_view direction) {
        if (!description.empty()) description += '+';
        description += direction;
    };
    if ((dpad & 0x01U) != 0) append("up");
    if ((dpad & 0x02U) != 0) append("down");
    if ((dpad & 0x04U) != 0) append("left");
    if ((dpad & 0x08U) != 0) append("right");
    return description;
}

} // namespace

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

int runInputStatus(asb::flydigi::Apex5Device& device,
                   unsigned long seconds,
                   bool json) {
    std::string error;
    auto input = asb::platform::openPhysicalInputSource(
        device.info(), std::nullopt, error);
    if (!input) {
        std::cerr << "Complete APEX input source unavailable: " << error << '\n';
        return 3;
    }
    const std::string warning = error;

    asb::dualsense::DualSenseInputState lastState{};
    std::uint64_t stateChanges = 0;
    std::uint8_t seenDpad = 0;
    bool receivedState = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline &&
           !g_stopRequested.load(std::memory_order_relaxed)) {
        asb::dualsense::DualSenseInputState state{};
        error.clear();
        const auto status = input->waitForState(state, std::chrono::milliseconds(250), error);
        if (status == asb::platform::PhysicalInputStatus::State) {
            if (!receivedState || state != lastState) ++stateChanges;
            seenDpad = static_cast<std::uint8_t>(seenDpad | state.dpad);
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
            << "  \"dpad_name\": \"" << dpadDescription(lastState.dpad) << "\",\n"
            << "  \"seen_dpad\": " << static_cast<unsigned int>(seenDpad) << ",\n"
            << "  \"seen_dpad_directions\": \"" << dpadDescription(seenDpad) << "\",\n"
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
                  << "dpad=" << static_cast<unsigned int>(lastState.dpad)
                  << " (" << dpadDescription(lastState.dpad) << ")\n"
                  << "seen_dpad=" << static_cast<unsigned int>(seenDpad)
                  << " (" << dpadDescription(seenDpad) << ")\n"
                  << "buttons=" << lastState.buttons << '\n';
        if (!warning.empty()) std::cout << "warning=" << warning << '\n';
    }
    return receivedState ? 0 : 5;
}

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
    return runInputStatus(*device, seconds, json);
}

int commandList() {
    std::string error;
    const auto candidates = asb::flydigi::Apex5Device::findCandidates(error);
    if (!error.empty()) {
        std::cerr << "HID enumeration warning: " << error << "\n";
    }
    if (candidates.empty()) {
        std::cout << "No APEX 4/5 vendor HID interface found.\n";
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
              << " (raw " << static_cast<unsigned int>(identity->connectionTypeRaw()) << ")\n";
    if (identity->hasBatteryLevel()) {
        std::cout << "Battery level: "
                  << static_cast<unsigned int>(identity->batteryLevel())
                  << (identity->isCharging() ? " (charging)" : "") << '\n';
    }
    std::cout << "Adaptive triggers: yes\n";
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

int runTestRt(asb::flydigi::Apex5Device& device) {
    std::string error;
    std::cout << "Using: " << narrowAscii(device.info().product) << " ("
              << hex16(device.info().vendorId) << ':' << hex16(device.info().productId) << ")\n";
    std::cout << "Applying a GENTLE RT resistance for about 1.5 seconds...\n";

    asb::TriggerResetGuard resetOnExit(device);

    // Exercise the same DualSense -> raw FORCEADAPT path as bridge-triggers.
    std::array<std::uint8_t, 11> dualSenseEffect{};
    dualSenseEffect[0] = 1;  // DualSense feedback/resistance
    dualSenseEffect[1] = 70; // start
    dualSenseEffect[2] = 30; // intentionally gentle resistance
    const auto translated = asb::dualsense::translateAdaptiveTrigger(
        asb::TriggerSide::Right, dualSenseEffect, 0);
    if (!translated || !device.setTriggerRaw(*translated, error)) {
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

    if (!device.clearAll(error)) {
        std::cerr << "WARNING: automatic reset write failed: " << error << "\n"
                  << "Open Flydigi Space Station and set both triggers to Normal before continuing.\n";
        return 5;
    }
    resetOnExit.dismiss();

    std::cout << "RT reset to Normal.\n"
              << "If you felt a resistance begin part-way through RT, the Windows -> APEX FORCEADAPT path works.\n";
    return 0;
}

int commandTestRt(int argc, char** argv) {
    std::string error;
    auto device = openSelected(argc, argv, error);
    if (!device) {
        std::cerr << "Error: " << error << "\n";
        return 3;
    }
    return runTestRt(*device);
}

int runTestRumble(asb::flydigi::Apex5Device& device) {
    std::string error;
    std::cout << "Using: " << narrowAscii(device.info().product) << " ("
              << hex16(device.info().vendorId) << ':' << hex16(device.info().productId) << ")\n";

    if (!device.stopRumble(error)) {
        std::cerr << "Could not establish a stopped rumble baseline: " << error << '\n';
        return 12;
    }
    asb::RumbleResetGuard resetOnExit(device);

    std::cout << "Applying a GENTLE low/high-frequency grip vibration for about 1 second...\n";
    constexpr std::uint8_t kGentleLowFrequency = 48;
    constexpr std::uint8_t kGentleHighFrequency = 32;
    if (!device.setRumble(kGentleLowFrequency, kGentleHighFrequency, error)) {
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

    if (!device.stopRumble(error)) {
        std::cerr << "WARNING: automatic rumble stop failed: " << error
                  << "\nPower-cycle the controller before continuing.\n";
        return 12;
    }
    resetOnExit.dismiss();
    std::cout << "Grip rumble stopped. If both handles vibrated gently, the APEX rumble command works.\n";
    return 0;
}

int commandTestRumble(int argc, char** argv) {
    std::string error;
    auto device = openSelected(argc, argv, error);
    if (!device) {
        std::cerr << "Error: " << error << "\n";
        return 3;
    }
    return runTestRumble(*device);
}

int commandApex4PortTest(int argc, char** argv) {
    std::optional<std::size_t> deviceIndex;
    unsigned long seconds = 10;
    bool testRumble = false;
    bool testForceAdapt = false;

    for (int index = 2; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--rumble") {
            testRumble = true;
        } else if (option == "--forceadapt") {
            testForceAdapt = true;
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
                std::cerr << "Unknown apex4-port-test option: " << option << "\n"
                          << "Usage: ApexSenseBridge apex4-port-test [index] "
                             "[--seconds N] [--rumble] [--forceadapt]\n";
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
    if (!device->identity() || !device->identity()->isApex4()) {
        std::cerr << "apex4-port-test requires a verified APEX 4.\n";
        return 3;
    }

    std::cout << "=== identity ===\n"
              << "Verified: " << device->identity()->describe() << '\n'
              << "Connection: "
              << (device->identity()->isWired() ? "wired" : "dongle")
              << " (raw "
              << static_cast<unsigned int>(device->identity()->connectionTypeRaw())
              << ")\n"
              << "Adaptive triggers: yes\n"
              << "=== input (" << seconds << " seconds) ===\n";

    const int inputCode = runInputStatus(*device, seconds, false);
    int rumbleCode = 0;
    int forceAdaptCode = 0;

    if (testRumble) {
        std::cout << "=== rumble ===\n";
        rumbleCode = runTestRumble(*device);
    }
    if (testForceAdapt) {
        std::cout << "=== forceadapt ===\n";
        forceAdaptCode = runTestRt(*device);
    }

    std::cout << "=== summary ===\n"
              << "input_exit_code=" << inputCode << '\n'
              << "rumble_requested=" << (testRumble ? "yes" : "no") << '\n'
              << "rumble_exit_code=" << rumbleCode << '\n'
              << "forceadapt_requested=" << (testForceAdapt ? "yes" : "no") << '\n'
              << "forceadapt_exit_code=" << forceAdaptCode << '\n';

    if (inputCode != 0) return inputCode;
    if (rumbleCode != 0) return rumbleCode;
    return forceAdaptCode;
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

} // namespace asb::cli
