#pragma once

#include "dualsense/TouchpadGestureProfile.h"
#include "dualsense/VirtualDualSense.h"

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace asb::cli {

struct ControllerCalibration {
    unsigned triggerStrength = 100;
    unsigned vibrationStrength = 100;
    unsigned threshold = 12;
    bool rumble = true;
    bool rgb = false;
    unsigned gyro = 100;
    unsigned gyroYaw = 100;
};

struct BridgeCommandOptions {
    std::optional<std::size_t> deviceIndex;
    std::optional<std::chrono::seconds> duration;
    std::filesystem::path viiperExecutable;
    asb::dualsense::VirtualDualSenseBackend virtualBackend =
        asb::dualsense::VirtualDualSenseBackend::Auto;
    bool routeRumble = false;
    bool syncLightbar = false;
    bool verifyVirtualInput = false;
    asb::dualsense::TouchpadGestureProfile touchpadProfile =
        asb::dualsense::TouchpadGestureProfile::None;
    unsigned int hapticThresholdPercent = 12;
    unsigned int triggerStrengthPercent = 100;
    unsigned int vibrationStrengthPercent = 100;
    unsigned int apex6HapticGainPercent = 100;
    bool apex6HapticGainExplicit = false;
    unsigned int apex4GyroStrengthPercent = 100;
    unsigned int apex4GyroYawStrengthPercent = 100;
    bool hapticThresholdExplicit = false;
    std::optional<unsigned int> xinputIndex;
    std::optional<std::string> sessionToken;
    std::optional<std::uint32_t> sessionOwnerProcessId;
    std::optional<std::uint8_t> apexProfileSlot;
    std::filesystem::path telemetryJson;
    // APEX4, APEX5, APEX6. Applied only after the device identity is verified.
    std::array<std::optional<ControllerCalibration>, 3> controllerCalibrations;
};

[[nodiscard]] bool applyControllerCalibration(
    BridgeCommandOptions& options, std::string_view verifiedModel, std::string& error);

[[nodiscard]] bool parseBridgeOptions(
    int argc,
    char** argv,
    BridgeCommandOptions& options,
    std::string& error);

[[nodiscard]] std::string_view bridgeCommandUsage() noexcept;

} // namespace asb::cli
