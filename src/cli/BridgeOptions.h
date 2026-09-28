#pragma once

#include "dualsense/TouchpadGestureProfile.h"
#include "dualsense/VirtualDualSense.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace asb::cli {

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
    bool hapticThresholdExplicit = false;
    std::optional<unsigned int> xinputIndex;
    std::optional<std::string> sessionToken;
    std::optional<std::uint32_t> sessionOwnerProcessId;
    std::optional<std::uint8_t> apexProfileSlot;
    std::filesystem::path telemetryJson;
};

[[nodiscard]] bool parseBridgeOptions(
    int argc,
    char** argv,
    BridgeCommandOptions& options,
    std::string& error);

[[nodiscard]] std::string_view bridgeCommandUsage() noexcept;

} // namespace asb::cli
