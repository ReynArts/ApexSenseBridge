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

} // namespace asb::cli

