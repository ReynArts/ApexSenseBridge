#pragma once

#include "dualsense/DualSenseInput.h"
#include "flydigi/Apex6Imu.h"
#include "platform/PhysicalInputActivity.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace asb::platform {

struct Apex6VendorTriggers {
    std::uint8_t left = 0;
    std::uint8_t right = 0;
    std::chrono::steady_clock::time_point at{};
};

// Keeps the APEX 6 mapped HID state authoritative while substituting the two
// independent XInput trigger axes. A bounded heartbeat makes an idle,
// event-driven HID collection observable without weakening the shared input
// freshness watchdog used by the other controller paths.
class Apex6InputFusion {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::uint8_t kActivityFloor = 16;
    static constexpr std::uint8_t kIndependentEngageFloor = 8;
    static constexpr std::uint8_t kIndependentReleaseFloor = 4;
    static constexpr auto kDeadConfirmation = std::chrono::milliseconds(120);
    static constexpr auto kVendorFreshness = std::chrono::milliseconds(100);

    explicit Apex6InputFusion(
        std::chrono::milliseconds heartbeat = std::chrono::milliseconds(250)) noexcept;

    void setXInputAvailable(bool available) noexcept;

    [[nodiscard]] std::optional<dualsense::DualSenseInputState> observe(
        const std::optional<dualsense::DualSenseInputState>& mappedState,
        const dualsense::DualSenseInputState& xinputState,
        Clock::time_point now) noexcept;

    [[nodiscard]] std::optional<dualsense::DualSenseInputState> observe(
        const std::optional<dualsense::DualSenseInputState>& mappedState,
        const dualsense::DualSenseInputState& xinputState,
        const std::optional<flydigi::apex6::MotionSample>& motion,
        Clock::time_point now) noexcept;

    [[nodiscard]] std::optional<dualsense::DualSenseInputState> observe(
        const std::optional<dualsense::DualSenseInputState>& mappedState,
        const dualsense::DualSenseInputState& xinputState,
        const std::optional<flydigi::apex6::MotionSample>& motion,
        const std::optional<Apex6VendorTriggers>& vendorTriggers,
        Clock::time_point now) noexcept;

    [[nodiscard]] bool fallbackActive() const noexcept { return fallbackActive_; }
    [[nodiscard]] const PhysicalTriggerSourceStats& triggerStats() const noexcept {
        return stats_;
    }

private:
    void setFallback(bool active) noexcept;
    void trackXInputHealth(const dualsense::DualSenseInputState& xinputState,
                           const std::optional<Apex6VendorTriggers>& vendorTriggers,
                           Clock::time_point now) noexcept;
    void applyFallbackTriggers(const std::optional<Apex6VendorTriggers>& vendorTriggers,
                               Clock::time_point now,
                               dualsense::DualSenseInputState& state) noexcept;

    std::chrono::milliseconds heartbeat_;
    std::optional<dualsense::DualSenseInputState> lastMappedState_;
    std::optional<dualsense::DualSenseInputState> lastPublishedState_;
    Clock::time_point lastPublishedAt_{};
    bool xinputAvailable_ = true;
    bool fallbackActive_ = false;
    bool independent_ = false;
    std::optional<Clock::time_point> releasedSince_;
    PhysicalTriggerSourceStats stats_{};
};

} // namespace asb::platform
