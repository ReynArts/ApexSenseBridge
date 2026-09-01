#pragma once

#include "dualsense/DualSenseInput.h"

#include <chrono>
#include <cstdint>

namespace asb::dualsense {

struct ViewTouchpadGestureStats {
    std::uint64_t taps = 0;
    std::uint64_t swipes = 0;
};

// XInput has one View/Back button but no touch surface. This transformer keeps
// a short View press as a DualSense touchpad click and turns a hold into the
// upward swipe used by Spider-Man 2 for its secondary touchpad action.
class ViewTouchpadGesture {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr auto kHoldThreshold = std::chrono::milliseconds(400);
    static constexpr auto kTapPulseDuration = std::chrono::milliseconds(32);
    static constexpr auto kSwipeDuration = std::chrono::milliseconds(180);

    void transform(DualSenseInputState& state, Clock::time_point now) noexcept;
    [[nodiscard]] ViewTouchpadGestureStats stats() const noexcept { return stats_; }

private:
    enum class Phase {
        Idle,
        Pending,
        TapPulse,
        Swipe,
        WaitForRelease,
    };

    void emitTap(DualSenseInputState& state) const noexcept;
    void emitSwipe(DualSenseInputState& state,
                   std::chrono::milliseconds elapsed) const noexcept;

    Phase phase_ = Phase::Idle;
    Clock::time_point phaseStarted_{};
    ViewTouchpadGestureStats stats_{};
};

} // namespace asb::dualsense
