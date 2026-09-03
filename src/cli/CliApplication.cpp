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

void printUsage() {
    std::cout
        << "ApexSenseBridge 0.5.0\n\n"
        << "Commands:\n"
        << "  list                         List APEX 4/5 vendor HID candidates\n"
        << "  diagnose [--all-hid] [--json]\n"
        << "                               Read-only HID interface diagnostic\n"
        << "  input-status [index] [--seconds N] [--json]\n"
        << "                               Validate the complete APEX input proxy source\n"
        << "  stop-active-sessions         Gracefully detach/restore an active bridge\n"
        << "  identify [index]             Read and verify the Flydigi APEX identity\n"
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
        << "  apex4-port-test [index] [--seconds N] [--rumble] [--forceadapt]\n"
        << "                               Validate APEX 4 input/effects with one identity exchange\n"
        << "  xinput-view-test [index] [--seconds N]\n"
        << "                               Measure View/Back hold duration without writes\n"
        << "  clear [index]                Clear LT/RT effects and stop grip rumble\n"
        << "  dry-run                      Print the test packet without HID I/O\n\n"
        << "Hardware writes only target a verified Apex 4 (04B4:2412, DInput) or\n"
        << "Apex 5 (Flydigi 37D7 controller family). Pass an index if several are found.\n"
        << "virtual-ds never opens the APEX HID interface and never routes feedback to it.\n";
}

int run(int argc, char** argv) {
    installConsoleHandler();

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
    if (command == "apex4-port-test") {
        return commandApex4PortTest(argc, argv);
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

} // namespace asb::cli
