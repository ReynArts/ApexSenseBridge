#pragma once

#include "dualsense/DualSenseInput.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace asb::platform {

// Keeps the APEX 6 mapped HID state authoritative while substituting the two
// independent XInput trigger axes. A bounded heartbeat makes an idle,
// event-driven HID collection observable without weakening the shared input
// freshness watchdog used by the other controller paths.
class Apex6InputFusion {
public:
    using Clock = std::chrono::steady_clock;

    explicit Apex6InputFusion(
        std::chrono::milliseconds heartbeat = std::chrono::milliseconds(250)) noexcept;

    [[nodiscard]] std::optional<dualsense::DualSenseInputState> observe(
        const std::optional<dualsense::DualSenseInputState>& mappedState,
        const dualsense::DualSenseInputState& xinputState,
        Clock::time_point now) noexcept;

private:
    std::chrono::milliseconds heartbeat_;
    std::optional<dualsense::DualSenseInputState> lastMappedState_;
    std::optional<dualsense::DualSenseInputState> lastPublishedState_;
    Clock::time_point lastPublishedAt_{};
};

} // namespace asb::platform
