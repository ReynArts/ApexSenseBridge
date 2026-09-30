#include "cli/BridgeOptions.h"

#include "flydigi/Apex5Protocol.h"
#include "platform/SessionControl.h"

#include <limits>
#include <stdexcept>

namespace asb::cli {
namespace {

std::optional<asb::dualsense::VirtualDualSenseBackend> parseBackend(
    std::string_view name) noexcept {
    using Backend = asb::dualsense::VirtualDualSenseBackend;
    if (name == "auto") return Backend::Auto;
    if (name == "integrated") return Backend::Integrated;
    if (name == "sidecar") return Backend::Sidecar;
    return std::nullopt;
}

} // namespace

bool parseBridgeOptions(int argc, char** argv, BridgeCommandOptions& options,
                        std::string& error) {
    for (int i = 2; i < argc; ++i) {
        const std::string_view value = argv[i];
        if (value == "--seconds") {
            if (++i >= argc) {
                error = "--seconds requires an integer from 1 to 86400.";
                return false;
            }
            try {
                std::size_t parsedCharacters = 0;
                const auto seconds = std::stoul(argv[i], &parsedCharacters);
                if (parsedCharacters != std::string_view(argv[i]).size() ||
                    seconds == 0 || seconds > 86400) {
                    throw std::out_of_range("seconds");
                }
                options.duration = std::chrono::seconds(seconds);
            } catch (...) {
                error = "--seconds requires an integer from 1 to 86400.";
                return false;
            }
        } else if (value == "--viiper") {
            if (++i >= argc) {
                error = "--viiper requires a path.";
                return false;
            }
            options.viiperExecutable = argv[i];
        } else if (value == "--virtual-backend") {
            if (++i >= argc) {
                error = "--virtual-backend requires auto, integrated, or sidecar.";
                return false;
            }
            const auto backend = parseBackend(argv[i]);
            if (!backend) {
                error = "--virtual-backend requires auto, integrated, or sidecar.";
                return false;
            }
            options.virtualBackend = *backend;
        } else if (value == "--telemetry-json") {
            if (++i >= argc) {
                error = "--telemetry-json requires a file path.";
                return false;
            }
            options.telemetryJson = argv[i];
        } else if (value == "--proxy-xinput" || value == "--isolate-apex") {
            // Compatibility aliases retained for older launchers. Full input
            // proxying and physical isolation are mandatory since 1.0.
        } else if (value == "--rumble") {
            options.routeRumble = true;
        } else if (value == "--sync-lightbar") {
            options.syncLightbar = true;
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
        } else if (value == "--trigger-strength" || value == "--vibration-strength") {
            if (++i >= argc) {
                error = std::string(value) + " requires an integer percentage from 0 to 100.";
                return false;
            }
            try {
                std::size_t consumed = 0;
                const auto parsed = std::stoul(argv[i], &consumed);
                if (consumed != std::string_view(argv[i]).size() || parsed > 100 || argv[i][0] == '-')
                    throw std::out_of_range("strength");
                (value == "--trigger-strength" ? options.triggerStrengthPercent : options.vibrationStrengthPercent) = static_cast<unsigned>(parsed);
            } catch (...) {
                error = std::string(value) + " requires an integer percentage from 0 to 100.";
                return false;
            }
        } else if (value == "--verify-virtual-input") {
            options.verifyVirtualInput = true;
        } else if (value == "--touchpad-profile") {
            if (++i >= argc) {
                error = "--touchpad-profile requires one of: none, spider-man-2, miles-morales, ghost-of-tsushima, warframe, death-stranding-2.";
                return false;
            }
            const auto profile = asb::dualsense::parseTouchpadGestureProfile(argv[i]);
            if (!profile || *profile ==
                                asb::dualsense::TouchpadGestureProfile::LegacyViewHoldSwipeUp) {
                error = "Unknown --touchpad-profile. Expected none, spider-man-2, miles-morales, ghost-of-tsushima, warframe, or death-stranding-2.";
                return false;
            }
            options.touchpadProfile = *profile;
        } else if (value == "--view-hold-swipe-up") {
            options.touchpadProfile =
                asb::dualsense::TouchpadGestureProfile::LegacyViewHoldSwipeUp;
        } else if (value == "--xinput-index") {
            if (++i >= argc) {
                error = "--xinput-index requires a value from 0 to 3.";
                return false;
            }
            try {
                std::size_t parsedCharacters = 0;
                const auto parsed = std::stoul(argv[i], &parsedCharacters);
                if (parsedCharacters != std::string_view(argv[i]).size() || parsed > 3) {
                    throw std::out_of_range("xinput-index");
                }
                options.xinputIndex = static_cast<unsigned int>(parsed);
            } catch (...) {
                error = "--xinput-index requires a value from 0 to 3.";
                return false;
            }
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
        } else if (value == "--session-owner-pid") {
            if (++i >= argc) {
                error = "--session-owner-pid requires a non-zero Windows process ID.";
                return false;
            }
            try {
                std::size_t parsedCharacters = 0;
                const auto parsed = std::stoull(argv[i], &parsedCharacters);
                if (parsedCharacters != std::string_view(argv[i]).size() ||
                    parsed == 0 || parsed > 0xFFFFFFFFULL) {
                    throw std::out_of_range("session-owner-pid");
                }
                options.sessionOwnerProcessId = static_cast<std::uint32_t>(parsed);
            } catch (...) {
                error = "--session-owner-pid requires a non-zero Windows process ID.";
                return false;
            }
        } else if (value == "--apex-profile") {
            if (++i >= argc) {
                error = "--apex-profile requires a profile number from 1 to 4.";
                return false;
            }
            try {
                std::size_t parsedCharacters = 0;
                const auto parsed = std::stoul(argv[i], &parsedCharacters);
                if (parsedCharacters != std::string_view(argv[i]).size() ||
                    parsed < 1 || parsed > asb::flydigi::kProfileSlotCount) {
                    throw std::out_of_range("apex-profile");
                }
                options.apexProfileSlot = static_cast<std::uint8_t>(parsed - 1);
            } catch (...) {
                error = "--apex-profile requires a profile number from 1 to 4.";
                return false;
            }
        } else if (!value.empty() && value.front() != '-' && !options.deviceIndex) {
            try {
                std::size_t parsedCharacters = 0;
                const auto parsed = std::stoull(argv[i], &parsedCharacters);
                if (parsedCharacters != value.size() ||
                    parsed > static_cast<unsigned long long>(
                                 (std::numeric_limits<std::size_t>::max)())) {
                    throw std::out_of_range("device-index");
                }
                options.deviceIndex = static_cast<std::size_t>(parsed);
            } catch (...) {
                error = "The device index must be an integer.";
                return false;
            }
        } else {
            error = "Unknown bridge-triggers option: " + std::string(value);
            return false;
        }
    }
    if (options.hapticThresholdExplicit && !options.routeRumble) {
        error = "--haptic-threshold requires --rumble.";
        return false;
    }
    if (options.sessionOwnerProcessId && !options.sessionToken) {
        error = "--session-owner-pid requires --session-token.";
        return false;
    }
    error.clear();
    return true;
}

std::string_view bridgeCommandUsage() noexcept {
    return "Usage: ApexSenseBridge bridge-triggers [index] [--seconds N] "
           "[--viiper PATH] [--virtual-backend auto|integrated|sidecar] "
           "[--telemetry-json PATH] [--xinput-index 0..3] [--rumble] "
           "[--sync-lightbar] [--haptic-threshold 0..95] "
           "[--trigger-strength 0..100] [--vibration-strength 0..100] "
           "[--verify-virtual-input] [--touchpad-profile NAME] "
           "[--view-hold-swipe-up] [--apex-profile 1..4] "
           "[--session-token 32HEX] [--session-owner-pid PID]";
}

} // namespace asb::cli
