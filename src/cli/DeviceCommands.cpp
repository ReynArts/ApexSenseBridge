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
#include <cmath>
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
            << "  \"gyro_x\": " << lastState.gyroX << ",\n"
            << "  \"gyro_y\": " << lastState.gyroY << ",\n"
            << "  \"gyro_z\": " << lastState.gyroZ << ",\n"
            << "  \"accel_x\": " << lastState.accelX << ",\n"
            << "  \"accel_y\": " << lastState.accelY << ",\n"
            << "  \"accel_z\": " << lastState.accelZ << ",\n"
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
                  << "buttons=" << lastState.buttons << '\n'
                  << "gyro=" << lastState.gyroX << ',' << lastState.gyroY << ',' << lastState.gyroZ << '\n'
                  << "accel=" << lastState.accelX << ',' << lastState.accelY << ',' << lastState.accelZ << '\n';
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
                  << " (" << static_cast<unsigned int>(identity->batteryPercent()) << "%)"
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

int runTestTrigger(asb::flydigi::Apex5Device& device,
                   std::string_view side,
                   std::string_view mode,
                   unsigned int level,
                   std::optional<std::uint8_t> customStart,
                   std::optional<std::uint8_t> customForce,
                   unsigned long seconds) {
    std::string error;
    std::cout << "Using: " << narrowAscii(device.info().product) << " ("
              << hex16(device.info().vendorId) << ':' << hex16(device.info().productId) << ")\n";

    asb::TriggerResetGuard resetOnExit(device);

    std::vector<asb::TriggerSide> targets;
    if (side == "lt" || side == "left" || side == "l2") {
        targets.push_back(asb::TriggerSide::Left);
    } else if (side == "rt" || side == "right" || side == "r2") {
        targets.push_back(asb::TriggerSide::Right);
    } else {
        targets.push_back(asb::TriggerSide::Left);
        targets.push_back(asb::TriggerSide::Right);
    }

    asb::ForceTriggerCommand cmd{};
    if (mode == "normal" || mode == "off" || mode == "clear") {
        cmd.mode = asb::TriggerMode::Normal;
    } else if (mode == "weapon" || mode == "break" || mode == "sniper") {
        cmd.mode = asb::TriggerMode::SniperBreak;
        std::uint8_t s = customStart.value_or(level == 1 ? 60 : (level == 2 ? 40 : (level == 3 ? 25 : 10)));
        std::uint8_t f = customForce.value_or(level == 1 ? 40 : (level == 2 ? 90 : (level == 3 ? 160 : 230)));
        std::uint8_t snap = level == 1 ? 60 : (level == 2 ? 130 : (level == 3 ? 200 : 255));
        cmd.params = {s, f, snap, 0, 0};
    } else if (mode == "vibration" || mode == "rattle" || mode == "recoil") {
        cmd.mode = asb::TriggerMode::RecoilRattle;
        std::uint8_t freq = level == 1 ? 10 : (level == 2 ? 20 : (level == 3 ? 30 : 40));
        std::uint8_t str = level == 1 ? 30 : (level == 2 ? 60 : (level == 3 ? 100 : 150));
        cmd.params = {freq, 1, str, 0, 0};
    } else if (mode == "bow") {
        cmd.mode = asb::TriggerMode::Race;
        std::uint8_t s = customStart.value_or(level == 1 ? 40 : (level == 2 ? 25 : (level == 3 ? 15 : 5)));
        std::uint8_t f = customForce.value_or(level == 1 ? 40 : (level == 2 ? 90 : (level == 3 ? 160 : 230)));
        cmd.params = {s, f, 0, 0, 0};
    } else { // default: resistance / race
        cmd.mode = asb::TriggerMode::Race;
        std::uint8_t s = customStart.value_or(level == 1 ? 60 : (level == 2 ? 40 : (level == 3 ? 20 : 5)));
        std::uint8_t f = customForce.value_or(level == 1 ? 30 : (level == 2 ? 80 : (level == 3 ? 160 : 240)));
        cmd.params = {s, f, 0, 0, 0};
    }

    std::cout << "Applying " << mode << " (level " << level << ") on " << side << " for " << seconds << "s...\n";

    for (auto targetSide : targets) {
        cmd.side = targetSide;
        if (!device.setTriggerRaw(cmd, error)) {
            std::cerr << "Write failed for " << (targetSide == asb::TriggerSide::Left ? "LT" : "RT") << ": " << error << "\n";
            return 4;
        }
    }

    if (seconds > 0) {
        const auto duration = std::chrono::seconds(seconds);
        constexpr auto slice = std::chrono::milliseconds(25);
        auto elapsed = std::chrono::milliseconds::zero();
        while (elapsed < duration && !g_stopRequested.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(slice);
            elapsed += slice;
        }

        if (!device.clearAll(error)) {
            std::cerr << "WARNING: automatic reset write failed: " << error << "\n";
            return 5;
        }
        resetOnExit.dismiss();
        std::cout << "Triggers reset to Normal.\n";
    } else {
        resetOnExit.dismiss();
        std::cout << "Triggers armed (hold mode until clear).\n";
    }
    return 0;
}

int commandTestTrigger(int argc, char** argv) {
    std::optional<std::size_t> deviceIndex;
    std::string side = "both";
    std::string mode = "resistance";
    unsigned int level = 2;
    unsigned long seconds = 2;
    std::optional<std::uint8_t> customStart;
    std::optional<std::uint8_t> customForce;

    for (int i = 2; i < argc; ++i) {
        std::string_view opt = argv[i];
        if (opt == "--side" && i + 1 < argc) {
            side = argv[++i];
        } else if (opt == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (opt == "--level" && i + 1 < argc) {
            try {
                level = std::clamp(static_cast<unsigned int>(std::stoul(argv[++i])), 1U, 4U);
            } catch (...) {}
        } else if (opt == "--seconds" && i + 1 < argc) {
            try {
                seconds = std::clamp(static_cast<unsigned long>(std::stoul(argv[++i])), 0UL, 60UL);
            } catch (...) {}
        } else if (opt == "--start" && i + 1 < argc) {
            try {
                customStart = static_cast<std::uint8_t>(std::clamp(std::stoul(argv[++i]), 0UL, 255UL));
            } catch (...) {}
        } else if (opt == "--force" && i + 1 < argc) {
            try {
                customForce = static_cast<std::uint8_t>(std::clamp(std::stoul(argv[++i]), 0UL, 255UL));
            } catch (...) {}
        } else if (!opt.empty() && opt[0] != '-' && !deviceIndex) {
            try {
                deviceIndex = static_cast<std::size_t>(std::stoul(std::string(opt)));
            } catch (...) {}
        }
    }

    std::string error;
    auto device = openSelectedIndex(deviceIndex, error);
    if (!device) {
        std::cerr << "Error: " << error << "\n";
        return 3;
    }
    return runTestTrigger(*device, side, mode, level, customStart, customForce, seconds);
}

int runTestRumble(asb::flydigi::Apex5Device& device,
                  std::uint8_t lowFrequency = 48,
                  std::uint8_t highFrequency = 32,
                  unsigned long seconds = 1) {
    std::string error;
    std::cout << "Using: " << narrowAscii(device.info().product) << " ("
              << hex16(device.info().vendorId) << ':' << hex16(device.info().productId) << ")\n";

    if (!device.stopRumble(error)) {
        std::cerr << "Could not establish a stopped rumble baseline: " << error << '\n';
        return 12;
    }
    asb::RumbleResetGuard resetOnExit(device);

    std::cout << "Applying grip vibration (L=" << static_cast<unsigned int>(lowFrequency)
              << ", R=" << static_cast<unsigned int>(highFrequency) << ") for " << seconds << "s...\n";
    if (!device.setRumble(lowFrequency, highFrequency, error)) {
        std::cerr << "Rumble write failed: " << error << '\n';
        return 12;
    }

    if (seconds > 0) {
        const auto duration = std::chrono::seconds(seconds);
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
        std::cout << "Grip rumble stopped.\n";
    } else {
        resetOnExit.dismiss();
        std::cout << "Grip rumble active (hold mode until clear).\n";
    }
    return 0;
}

int commandTestRumble(int argc, char** argv) {
    std::optional<std::size_t> deviceIndex;
    std::uint8_t leftMotor = 48;
    std::uint8_t rightMotor = 32;
    unsigned long seconds = 1;

    for (int i = 2; i < argc; ++i) {
        std::string_view opt = argv[i];
        if (opt == "--left" && i + 1 < argc) {
            try {
                leftMotor = static_cast<std::uint8_t>(std::clamp(std::stoul(argv[++i]), 0UL, 255UL));
            } catch (...) {}
        } else if (opt == "--right" && i + 1 < argc) {
            try {
                rightMotor = static_cast<std::uint8_t>(std::clamp(std::stoul(argv[++i]), 0UL, 255UL));
            } catch (...) {}
        } else if (opt == "--seconds" && i + 1 < argc) {
            try {
                seconds = std::clamp(static_cast<unsigned long>(std::stoul(argv[++i])), 0UL, 60UL);
            } catch (...) {}
        } else if (!opt.empty() && opt[0] != '-' && !deviceIndex) {
            try {
                deviceIndex = static_cast<std::size_t>(std::stoul(std::string(opt)));
            } catch (...) {}
        }
    }

    std::string error;
    auto device = openSelectedIndex(deviceIndex, error);
    if (!device) {
        std::cerr << "Error: " << error << "\n";
        return 3;
    }
    return runTestRumble(*device, leftMotor, rightMotor, seconds);
}

int commandTestProfileSwitch(int argc, char** argv) {
    std::optional<std::size_t> deviceIndex;
    std::optional<std::uint8_t> requestedTarget;
    for (int index = 2; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--target") {
            if (++index >= argc || requestedTarget) {
                std::cerr << "--target requires one profile number from 1 to 4.\n";
                return 1;
            }
            try {
                std::size_t parsedCharacters = 0;
                const auto parsed = std::stoul(argv[index], &parsedCharacters);
                if (parsedCharacters != std::string_view(argv[index]).size() ||
                    parsed < 1 || parsed > asb::flydigi::kProfileSlotCount) {
                    throw std::out_of_range("target");
                }
                requestedTarget = static_cast<std::uint8_t>(parsed - 1);
            } catch (...) {
                std::cerr << "--target requires one profile number from 1 to 4.\n";
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
                std::cerr << "Unknown test-profile-switch option: " << option << "\n"
                          << "Usage: ApexSenseBridge test-profile-switch [index] "
                             "[--target 1..4]\n";
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
    if (!device->identity() || !device->identity()->isApex5()) {
        std::cerr << "test-profile-switch supports Apex 5 only; no profile command was sent.\n";
        return 3;
    }

    asb::flydigi::ProfileStatus initial{};
    asb::flydigi::ProfileStatus confirmation{};
    if (!device->readProfileStatus(initial, error)) {
        std::cerr << "Profile preflight failed before any switch: " << error << '\n';
        return 4;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    error.clear();
    if (!device->readProfileStatus(confirmation, error)) {
        std::cerr << "Second profile preflight failed before any switch: " << error << '\n';
        return 4;
    }
    if (initial.rawSlot != confirmation.rawSlot) {
        std::cerr << "Profile preflight was unstable (raw slots "
                  << static_cast<unsigned int>(initial.rawSlot) << " then "
                  << static_cast<unsigned int>(confirmation.rawSlot)
                  << "); no profile switch was attempted.\n";
        return 4;
    }
    if (initial.switchBank) {
        std::cerr << "The controller is using its Nintendo Switch profile bank; "
                     "this XInput-only diagnostic refuses to change it.\n";
        return 4;
    }

    const auto target = requestedTarget.value_or(static_cast<std::uint8_t>(
        (initial.slot + 1) % asb::flydigi::kProfileSlotCount));
    if (target == initial.slot) {
        std::cerr << "Target profile " << static_cast<unsigned int>(target + 1)
                  << " is already active; choose a different --target.\n";
        return 1;
    }

    std::cout << "Using: " << narrowAscii(device->info().product) << " ("
              << hex16(device->info().vendorId) << ':'
              << hex16(device->info().productId) << ")\n"
              << "Preflight passed twice: profile "
              << static_cast<unsigned int>(initial.slot + 1) << " is active.\n"
              << "No profile data will be saved or overwritten.\n"
              << "Temporarily switching to profile "
              << static_cast<unsigned int>(target + 1) << "...\n";

    // From this point onward the apply command may have reached the pad even
    // when another process consumes its acknowledgement. Every path below
    // therefore attempts and verifies restoration of the original slot.
    error.clear();
    const bool switchAcknowledged = device->applyProfile(target, error);
    const std::string switchAckError = error;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    asb::flydigi::ProfileStatus switched{};
    std::string switchStatusError;
    const bool switchStatusRead =
        device->readProfileStatus(switched, switchStatusError);
    const bool switchVerified = switchStatusRead &&
                                !switched.switchBank &&
                                switched.slot == target;

    if (switchVerified) {
        std::cout << "Temporary switch verified: profile "
                  << static_cast<unsigned int>(target + 1)
                  << " is active for about 1 second.\n";
        constexpr auto kHold = std::chrono::milliseconds(1000);
        constexpr auto kSlice = std::chrono::milliseconds(25);
        auto elapsed = std::chrono::milliseconds::zero();
        while (elapsed < kHold &&
               !g_stopRequested.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(kSlice);
            elapsed += kSlice;
        }
    } else {
        std::cerr << "Temporary switch could not be verified";
        if (!switchAcknowledged && !switchAckError.empty()) {
            std::cerr << ": " << switchAckError;
        } else if (!switchStatusRead && !switchStatusError.empty()) {
            std::cerr << ": " << switchStatusError;
        } else if (switchStatusRead) {
            std::cerr << ": controller reported raw slot "
                      << static_cast<unsigned int>(switched.rawSlot);
        }
        std::cerr << ". Restoring the original profile now.\n";
    }

    bool restored = false;
    std::string restoreError;
    for (int attempt = 1; attempt <= 3 && !restored; ++attempt) {
        std::string applyError;
        const bool restoreAcknowledged =
            device->applyProfile(initial.slot, applyError);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        asb::flydigi::ProfileStatus restoredStatus{};
        std::string statusError;
        const bool statusRead =
            device->readProfileStatus(restoredStatus, statusError);
        restored = statusRead && !restoredStatus.switchBank &&
                   restoredStatus.slot == initial.slot;
        if (!restored) {
            std::ostringstream detail;
            detail << "restore attempt " << attempt << " failed";
            if (!restoreAcknowledged && !applyError.empty()) {
                detail << ": " << applyError;
            } else if (!statusRead && !statusError.empty()) {
                detail << ": " << statusError;
            } else if (statusRead) {
                detail << ": controller reported raw slot "
                       << static_cast<unsigned int>(restoredStatus.rawSlot);
            }
            restoreError = detail.str();
        }
    }

    if (!restored) {
        std::cerr << "RESTORE NOT VERIFIED: " << restoreError << "\n"
                  << "Use the controller's profile shortcut now to return manually to profile "
                  << static_cast<unsigned int>(initial.slot + 1) << ".\n";
        return 14;
    }

    std::cout << "Restore verified: profile "
              << static_cast<unsigned int>(initial.slot + 1)
              << " is active again.\n";
    if (!switchVerified) return 13;
    if (!switchAcknowledged) {
        std::cout << "Note: the apply acknowledgement was missed, but both status "
                     "verification and restoration succeeded.\n";
    }
    std::cout << "Profile-switch diagnostic passed.\n";
    return 0;
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

int commandTestRgb(int argc, char** argv) {
    std::optional<std::size_t> deviceIndex;
    std::vector<std::string_view> colorArgs;

    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (colorArgs.empty() && !deviceIndex) {
            try {
                std::size_t parsedCharacters = 0;
                const auto parsed = std::stoul(std::string(arg), &parsedCharacters);
                if (parsedCharacters == arg.size()) {
                    if (arg.size() == 1 || (arg.size() <= 2 && parsed < 16)) {
                        deviceIndex = static_cast<std::size_t>(parsed);
                        continue;
                    }
                }
            } catch (...) {
            }
        }
        colorArgs.push_back(arg);
    }

    std::string error;
    auto device = openSelectedIndex(deviceIndex, error);
    if (!device) {
        std::cerr << "Error: " << error << '\n';
        return 3;
    }

    struct RgbStep {
        std::string name;
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
    };
    std::vector<RgbStep> steps;

    if (colorArgs.empty()) {
        steps = {
            {"Red", 255, 0, 0},
            {"Green", 0, 255, 0},
            {"Blue", 0, 0, 255},
            {"White", 255, 255, 255}
        };
    } else if (colorArgs.size() == 1) {
        std::string_view hex = colorArgs[0];
        if (!hex.empty() && hex[0] == '#') hex.remove_prefix(1);
        if (hex.size() != 6) {
            std::cerr << "Invalid hex color format: " << colorArgs[0]
                      << " (expected #RRGGBB or RRGGBB)\n";
            return 1;
        }
        try {
            const unsigned long value = std::stoul(std::string(hex), nullptr, 16);
            steps.push_back({
                std::string(colorArgs[0]),
                static_cast<std::uint8_t>((value >> 16) & 0xFF),
                static_cast<std::uint8_t>((value >> 8) & 0xFF),
                static_cast<std::uint8_t>(value & 0xFF)
            });
        } catch (...) {
            std::cerr << "Failed to parse hex color: " << colorArgs[0] << '\n';
            return 1;
        }
    } else if (colorArgs.size() == 3) {
        try {
            const auto r = std::stoul(std::string(colorArgs[0]));
            const auto g = std::stoul(std::string(colorArgs[1]));
            const auto b = std::stoul(std::string(colorArgs[2]));
            if (r > 255 || g > 255 || b > 255) throw std::out_of_range("rgb");
            steps.push_back({
                "Custom",
                static_cast<std::uint8_t>(r),
                static_cast<std::uint8_t>(g),
                static_cast<std::uint8_t>(b)
            });
        } catch (...) {
            std::cerr << "Invalid RGB values (expected 0-255 for R, G, B)\n";
            return 1;
        }
    } else {
        std::cerr << "Usage: ApexSenseBridge test-rgb [index] [R G B | #RRGGBB]\n";
        return 1;
    }

    asb::flydigi::ProfileStatus originalProfile{};
    const bool hasProfile = device->readProfileStatus(originalProfile, error);
    error.clear();

    const std::uint8_t activeSlot = hasProfile ? originalProfile.slot : 0;

    std::array<std::uint8_t, asb::flydigi::kRgbConfigSize> backupConfig{};
    const bool hasBackup = device->readRgbConfig(activeSlot, backupConfig, error);
    error.clear();

    struct RgbRestoreGuard {
        asb::flydigi::Apex5Device* dev = nullptr;
        std::uint8_t slot = 0;
        std::array<std::uint8_t, asb::flydigi::kRgbConfigSize> config{};
        bool backup = false;
        bool profile = false;
        ~RgbRestoreGuard() {
            if (dev) {
                std::string err;
                if (backup) dev->writeRgbConfig(slot, config, err);
                if (profile) dev->applyProfile(slot, err);
            }
        }
    } guard{&(*device), activeSlot, backupConfig, hasBackup, hasProfile};

    for (const auto& step : steps) {
        if (g_stopRequested.load(std::memory_order_relaxed)) break;

        std::cout << "Setting RGB: " << step.name << " ("
                  << static_cast<int>(step.r) << ", "
                  << static_cast<int>(step.g) << ", "
                  << static_cast<int>(step.b) << ")... ";

        if (!device->setRgb(step.r, step.g, step.b, error, activeSlot)) {
            std::cout << "FAILED: " << error << '\n';
            return 12;
        }
        std::cout << "OK\n";

        constexpr auto stepDuration = std::chrono::milliseconds(2000);
        constexpr auto slice = std::chrono::milliseconds(25);
        auto elapsed = std::chrono::milliseconds::zero();
        while (elapsed < stepDuration && !g_stopRequested.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(slice);
            elapsed += slice;
        }
    }

    std::cout << "Restoring controller profile lighting... ";
    if (hasBackup) {
        device->writeRgbConfig(activeSlot, backupConfig, error);
    }
    if (hasProfile) {
        device->applyProfile(originalProfile.slot, error);
    }
    guard.dev = nullptr;
    std::cout << "OK\n";
    return 0;
}

int commandTestGyro(int argc, char** argv) {
    std::optional<std::size_t> deviceIndex;
    unsigned long seconds = 3;
    bool json = false;
    bool stream = false;

    for (int index = 2; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--json") {
            json = true;
        } else if (option == "--stream") {
            stream = true;
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
        } else if (option.rfind("--", 0) == 0) {
            std::cerr << "Unknown option: " << option << '\n';
            return 1;
        } else if (!deviceIndex) {
            try {
                std::size_t parsedCharacters = 0;
                const auto parsed = std::stoul(std::string(option), &parsedCharacters);
                if (parsedCharacters != option.size()) {
                    throw std::invalid_argument("trailing characters");
                }
                deviceIndex = parsed;
            } catch (...) {
                std::cerr << "Invalid device index: " << option << '\n';
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
                std::cerr << "Unknown test-gyro option: " << option << "\n"
                          << "Usage: ApexSenseBridge test-gyro [index] [--seconds N] [--stream] [--json]\n";
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

    auto input = asb::platform::openPhysicalInputSource(device->info(), std::nullopt, error);
    if (!input) {
        std::cerr << "APEX input source unavailable: " << error << '\n';
        return 3;
    }

    const auto deadline = stream
        ? (std::chrono::steady_clock::now() + std::chrono::hours(24))
        : (std::chrono::steady_clock::now() + std::chrono::seconds(seconds));

    std::uint64_t sampleCount = 0;
    std::int16_t minGyroX = (std::numeric_limits<std::int16_t>::max)();
    std::int16_t maxGyroX = (std::numeric_limits<std::int16_t>::min)();
    std::int16_t minGyroY = (std::numeric_limits<std::int16_t>::max)();
    std::int16_t maxGyroY = (std::numeric_limits<std::int16_t>::min)();
    std::int16_t minGyroZ = (std::numeric_limits<std::int16_t>::max)();
    std::int16_t maxGyroZ = (std::numeric_limits<std::int16_t>::min)();

    asb::dualsense::DualSenseInputState lastState{};
    bool received = false;

    while ((stream || std::chrono::steady_clock::now() < deadline) &&
           !g_stopRequested.load(std::memory_order_relaxed)) {
        asb::dualsense::DualSenseInputState state{};
        error.clear();
        const auto status = input->waitForState(state, std::chrono::milliseconds(200), error);
        if (status == asb::platform::PhysicalInputStatus::State) {
            received = true;
            ++sampleCount;
            lastState = state;
            minGyroX = (std::min)(minGyroX, state.gyroX);
            maxGyroX = (std::max)(maxGyroX, state.gyroX);
            minGyroY = (std::min)(minGyroY, state.gyroY);
            maxGyroY = (std::max)(maxGyroY, state.gyroY);
            minGyroZ = (std::min)(minGyroZ, state.gyroZ);
            maxGyroZ = (std::max)(maxGyroZ, state.gyroZ);

            if (stream) {
                const double ax = state.accelX / 10000.0;
                const double ay = state.accelY / 10000.0;
                const double az = state.accelZ / 10000.0;
                const double pitchDeg = std::atan2(-ay, std::sqrt(ax * ax + az * az)) * (180.0 / 3.141592653589793);
                const double rollDeg = std::atan2(ax, az) * (180.0 / 3.141592653589793);

                std::cout << "GYRO:" << state.gyroX << ',' << state.gyroY << ',' << state.gyroZ
                          << " ACCEL:" << state.accelX << ',' << state.accelY << ',' << state.accelZ
                          << " PITCH:" << static_cast<int>(pitchDeg)
                          << " ROLL:" << static_cast<int>(rollDeg)
                          << std::endl;
            }
        } else if (status == asb::platform::PhysicalInputStatus::Disconnected ||
                   status == asb::platform::PhysicalInputStatus::Error) {
            if (!stream) {
                std::cerr << "APEX input stream error: " << error << '\n';
                return 4;
            }
            break;
        }
    }

    if (stream) {
        return 0;
    }

    const double ax = lastState.accelX / 10000.0;
    const double ay = lastState.accelY / 10000.0;
    const double az = lastState.accelZ / 10000.0;
    const double pitchDeg = std::atan2(-ay, std::sqrt(ax * ax + az * az)) * (180.0 / 3.141592653589793);
    const double rollDeg = std::atan2(ax, az) * (180.0 / 3.141592653589793);

    const bool motionDetected = (maxGyroX - minGyroX > 15) ||
                                (maxGyroY - minGyroY > 15) ||
                                (maxGyroZ - minGyroZ > 15);

    if (json) {
        std::cout << "{\n"
                  << "  \"backend\": \"" << jsonEscape(input->backendName()) << "\",\n"
                  << "  \"received\": " << (received ? "true" : "false") << ",\n"
                  << "  \"samples\": " << sampleCount << ",\n"
                  << "  \"motion_detected\": " << (motionDetected ? "true" : "false") << ",\n"
                  << "  \"gyro_x\": " << lastState.gyroX << ",\n"
                  << "  \"gyro_y\": " << lastState.gyroY << ",\n"
                  << "  \"gyro_z\": " << lastState.gyroZ << ",\n"
                  << "  \"min_gyro_x\": " << (sampleCount ? minGyroX : 0) << ",\n"
                  << "  \"max_gyro_x\": " << (sampleCount ? maxGyroX : 0) << ",\n"
                  << "  \"accel_x\": " << lastState.accelX << ",\n"
                  << "  \"accel_y\": " << lastState.accelY << ",\n"
                  << "  \"accel_z\": " << lastState.accelZ << ",\n"
                  << "  \"pitch_deg\": " << static_cast<int>(pitchDeg) << ",\n"
                  << "  \"roll_deg\": " << static_cast<int>(rollDeg) << "\n"
                  << "}\n";
    } else {
        std::cout << "--- 6-Axis Motion Sensor Diagnostic ---\n"
                  << "Backend: " << input->backendName() << '\n'
                  << "Samples collected: " << sampleCount << '\n'
                  << "Motion detected: " << (motionDetected ? "YES" : "NO (still)") << '\n'
                  << "Latest Gyro (deg/s units): X=" << lastState.gyroX << " Y=" << lastState.gyroY << " Z=" << lastState.gyroZ << '\n'
                  << "Latest Accel (10000=1G):   X=" << lastState.accelX << " Y=" << lastState.accelY << " Z=" << lastState.accelZ << '\n'
                  << "Orientation: Pitch=" << static_cast<int>(pitchDeg) << " deg  Roll=" << static_cast<int>(rollDeg) << " deg\n";
    }

    return received ? 0 : 5;
}

} // namespace asb::cli
