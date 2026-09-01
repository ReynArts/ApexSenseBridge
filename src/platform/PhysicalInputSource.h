#pragma once

#include "core/DeviceInfo.h"
#include "dualsense/DualSenseInput.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace asb::platform {

enum class PhysicalInputStatus {
    State,
    Timeout,
    Disconnected,
    Error,
};

struct PhysicalInputSourceStats {
    std::uint64_t reports = 0;
    std::uint64_t timeouts = 0;
    std::uint64_t parseFailures = 0;
};

// Reads the complete physical APEX game state for the mandatory
// APEX -> bridge -> virtual DualSense path. A game must never consume this
// physical source directly while a DualSense bridge session is active.
class PhysicalInputSource {
public:
    virtual ~PhysicalInputSource() = default;

    virtual PhysicalInputStatus waitForState(
        dualsense::DualSenseInputState& state,
        std::chrono::milliseconds timeout,
        std::string& error) = 0;

    [[nodiscard]] virtual std::string_view backendName() const noexcept = 0;
    [[nodiscard]] virtual bool eventDriven() const noexcept = 0;
    [[nodiscard]] virtual PhysicalInputSourceStats stats() const noexcept = 0;
};

// Uses the game-controller HID collection belonging to the same Windows
// container as apexVendorInterface. --xinput-index remains an advanced escape
// hatch and explicitly selects the polling fallback.
std::unique_ptr<PhysicalInputSource> openPhysicalInputSource(
    const HidDeviceInfo& apexVendorInterface,
    std::optional<unsigned int> requestedXInputIndex,
    std::string& error);

} // namespace asb::platform
