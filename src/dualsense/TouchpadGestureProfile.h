#pragma once

#include "dualsense/DualSenseInput.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace asb::dualsense {

enum class TouchpadGestureProfile {
    None,
    LegacyViewHoldSwipeUp,
    SpiderMan2,
    MilesMorales,
    GhostOfTsushima,
    Warframe,
};

enum class TouchpadSwipeDirection : std::size_t {
    Up = 0,
    Down = 1,
    Left = 2,
    Right = 3,
};

[[nodiscard]] std::optional<TouchpadGestureProfile> parseTouchpadGestureProfile(
    std::string_view name) noexcept;
[[nodiscard]] std::string_view touchpadGestureProfileName(
    TouchpadGestureProfile profile) noexcept;

struct TouchpadGestureProfileStats {
    std::uint64_t replayedTaps = 0;
    std::uint64_t swipes = 0;
    std::array<std::uint64_t, 4> swipesByDirection{};
};

// Converts the documented Xbox fallback controls of a known game back into
// the touch gestures expected when that same game sees the virtual DualSense.
// Unknown games use Profile::None and are left completely untouched.
class TouchpadGestureMapper {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr auto kHoldThreshold = std::chrono::milliseconds(400);
    static constexpr auto kTapPulseDuration = std::chrono::milliseconds(32);
    static constexpr auto kSwipeDuration = std::chrono::milliseconds(180);

    enum class HoldSource {
        View,
        DpadUp,
    };

    explicit TouchpadGestureMapper(
        TouchpadGestureProfile profile = TouchpadGestureProfile::None) noexcept
        : profile_(profile) {}

    void transform(DualSenseInputState& state, Clock::time_point now) noexcept;

    [[nodiscard]] TouchpadGestureProfile profile() const noexcept { return profile_; }
    [[nodiscard]] TouchpadGestureProfileStats stats() const noexcept { return stats_; }

private:
    enum class HoldPhase {
        Idle,
        Pending,
        TapPulse,
        WaitForRelease,
    };

    struct HoldState {
        HoldPhase phase = HoldPhase::Idle;
        Clock::time_point started{};
    };

    struct SwipeState {
        bool active = false;
        TouchpadSwipeDirection direction = TouchpadSwipeDirection::Up;
        Clock::time_point started{};
    };

    void transformHold(HoldState& hold, HoldSource source,
                       TouchpadSwipeDirection direction,
                       DualSenseInputState& state,
                       Clock::time_point now) noexcept;
    void transformGhost(DualSenseInputState& state,
                        Clock::time_point now) noexcept;
    void transformWarframe(DualSenseInputState& state,
                           Clock::time_point now) noexcept;
    void startSwipe(TouchpadSwipeDirection direction,
                    Clock::time_point now) noexcept;
    void emitSwipe(DualSenseInputState& state,
                   Clock::time_point now) noexcept;

    TouchpadGestureProfile profile_;
    TouchpadGestureProfileStats stats_{};
    HoldState viewHold_{};
    HoldState dpadUpHold_{};
    SwipeState swipe_{};

    bool ghostModifierWasPressed_ = false;
    bool ghostArmed_ = false;
    bool ghostFired_ = false;
    Clock::time_point ghostTapStarted_{};
    bool ghostTapActive_ = false;

    bool warframeModifierWasPressed_ = false;
    bool warframeArmed_ = false;
    bool warframeFired_ = false;
    bool warframePassthrough_ = false;
    std::uint16_t warframeConsumedFace_ = 0;
    Clock::time_point warframeTapStarted_{};
    bool warframeTapActive_ = false;
};

} // namespace asb::dualsense
