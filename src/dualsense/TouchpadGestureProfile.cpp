#include "dualsense/TouchpadGestureProfile.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>

namespace asb::dualsense {
namespace {

constexpr std::uint16_t kTouchCenterX = 960;
constexpr std::uint16_t kTouchCenterY = 540;
constexpr std::uint16_t kTouchLeftX = 320;
constexpr std::uint16_t kTouchRightX = 1600;
constexpr std::uint16_t kTouchTopY = 200;
constexpr std::uint16_t kTouchBottomY = 850;
constexpr int kStickDirectionThreshold = 64;

bool sourcePressed(const DualSenseInputState& state,
                   TouchpadGestureMapper::HoldSource source) noexcept {
    if (source == TouchpadGestureMapper::HoldSource::View) {
        return (state.buttons & button::kTouchpadClick) != 0;
    }
    return (state.dpad & 0x01U) != 0;
}

void suppressSource(DualSenseInputState& state,
                    TouchpadGestureMapper::HoldSource source) noexcept {
    if (source == TouchpadGestureMapper::HoldSource::View) {
        state.buttons = static_cast<std::uint16_t>(
            state.buttons & ~button::kTouchpadClick);
    } else {
        state.dpad = static_cast<std::uint8_t>(state.dpad & ~0x01U);
    }
}

void emitSourceTap(DualSenseInputState& state,
                   TouchpadGestureMapper::HoldSource source) noexcept {
    if (source == TouchpadGestureMapper::HoldSource::View) {
        state.buttons = static_cast<std::uint16_t>(
            state.buttons | button::kTouchpadClick);
        state.touch1X = kTouchCenterX;
        state.touch1Y = kTouchCenterY;
        state.touch1Active = true;
    } else {
        state.dpad = static_cast<std::uint8_t>(state.dpad | 0x01U);
    }
}

bool rightStickNeutral(const DualSenseInputState& state) noexcept {
    return std::abs(static_cast<int>(state.rx) - 128) < kStickDirectionThreshold &&
           std::abs(static_cast<int>(state.ry) - 128) < kStickDirectionThreshold;
}

std::optional<TouchpadSwipeDirection> rightStickDirection(
    const DualSenseInputState& state) noexcept {
    const int x = static_cast<int>(state.rx) - 128;
    const int y = static_cast<int>(state.ry) - 128;
    const int absX = std::abs(x);
    const int absY = std::abs(y);
    if (std::max(absX, absY) < kStickDirectionThreshold) return std::nullopt;
    if (absX > absY) {
        return x < 0 ? TouchpadSwipeDirection::Left
                     : TouchpadSwipeDirection::Right;
    }
    return y < 0 ? TouchpadSwipeDirection::Up
                 : TouchpadSwipeDirection::Down;
}

std::optional<std::pair<TouchpadSwipeDirection, std::uint16_t>> warframeFace(
    std::uint16_t buttons) noexcept {
    // Xbox A/X/B/Y become DualSense Cross/Square/Circle/Triangle in the
    // physical proxy. The directions mirror Warframe's four touch powers.
    constexpr std::array<std::pair<std::uint16_t, TouchpadSwipeDirection>, 4> map{{
        {button::kCross, TouchpadSwipeDirection::Up},
        {button::kSquare, TouchpadSwipeDirection::Down},
        {button::kCircle, TouchpadSwipeDirection::Left},
        {button::kTriangle, TouchpadSwipeDirection::Right},
    }};
    for (const auto& [face, direction] : map) {
        if ((buttons & face) != 0) return std::pair{direction, face};
    }
    return std::nullopt;
}

} // namespace

std::optional<TouchpadGestureProfile> parseTouchpadGestureProfile(
    std::string_view name) noexcept {
    if (name == "none") return TouchpadGestureProfile::None;
    if (name == "legacy-view-hold-swipe-up") {
        return TouchpadGestureProfile::LegacyViewHoldSwipeUp;
    }
    if (name == "spider-man-2") return TouchpadGestureProfile::SpiderMan2;
    if (name == "miles-morales") return TouchpadGestureProfile::MilesMorales;
    if (name == "ghost-of-tsushima") return TouchpadGestureProfile::GhostOfTsushima;
    if (name == "warframe") return TouchpadGestureProfile::Warframe;
    return std::nullopt;
}

std::string_view touchpadGestureProfileName(TouchpadGestureProfile profile) noexcept {
    switch (profile) {
    case TouchpadGestureProfile::None: return "none";
    case TouchpadGestureProfile::LegacyViewHoldSwipeUp:
        return "legacy-view-hold-swipe-up";
    case TouchpadGestureProfile::SpiderMan2: return "spider-man-2";
    case TouchpadGestureProfile::MilesMorales: return "miles-morales";
    case TouchpadGestureProfile::GhostOfTsushima: return "ghost-of-tsushima";
    case TouchpadGestureProfile::Warframe: return "warframe";
    }
    return "none";
}

void TouchpadGestureMapper::startSwipe(TouchpadSwipeDirection direction,
                                       Clock::time_point now) noexcept {
    if (swipe_.active) return;
    swipe_.active = true;
    swipe_.direction = direction;
    swipe_.started = now;
    ++stats_.swipes;
    ++stats_.swipesByDirection[static_cast<std::size_t>(direction)];
}

void TouchpadGestureMapper::emitSwipe(DualSenseInputState& state,
                                      Clock::time_point now) noexcept {
    if (!swipe_.active) return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - swipe_.started);
    if (elapsed >= kSwipeDuration) {
        swipe_.active = false;
        return;
    }
    const auto bounded = std::clamp(elapsed, std::chrono::milliseconds::zero(),
                                    kSwipeDuration);
    const auto interpolate = [bounded](std::uint16_t from, std::uint16_t to) {
        const auto distance = static_cast<std::int64_t>(to) - from;
        return static_cast<std::uint16_t>(
            static_cast<std::int64_t>(from) +
            distance * bounded.count() / kSwipeDuration.count());
    };

    std::uint16_t fromX = kTouchCenterX;
    std::uint16_t toX = kTouchCenterX;
    std::uint16_t fromY = kTouchCenterY;
    std::uint16_t toY = kTouchCenterY;
    switch (swipe_.direction) {
    case TouchpadSwipeDirection::Up:
        fromY = kTouchBottomY;
        toY = kTouchTopY;
        break;
    case TouchpadSwipeDirection::Down:
        fromY = kTouchTopY;
        toY = kTouchBottomY;
        break;
    case TouchpadSwipeDirection::Left:
        fromX = kTouchRightX;
        toX = kTouchLeftX;
        break;
    case TouchpadSwipeDirection::Right:
        fromX = kTouchLeftX;
        toX = kTouchRightX;
        break;
    }
    state.buttons = static_cast<std::uint16_t>(
        state.buttons & ~button::kTouchpadClick);
    state.touch1X = interpolate(fromX, toX);
    state.touch1Y = interpolate(fromY, toY);
    state.touch1Active = true;
}

void TouchpadGestureMapper::transformHold(
    HoldState& hold, HoldSource source, TouchpadSwipeDirection direction,
    DualSenseInputState& state, Clock::time_point now) noexcept {
    const bool pressed = sourcePressed(state, source);
    suppressSource(state, source);

    switch (hold.phase) {
    case HoldPhase::Idle:
        if (pressed) {
            hold.phase = HoldPhase::Pending;
            hold.started = now;
        }
        return;
    case HoldPhase::Pending:
        if (!pressed) {
            hold.phase = HoldPhase::TapPulse;
            hold.started = now;
            ++stats_.replayedTaps;
            emitSourceTap(state, source);
            return;
        }
        if (now - hold.started >= kHoldThreshold) {
            hold.phase = HoldPhase::WaitForRelease;
            startSwipe(direction, now);
        }
        return;
    case HoldPhase::TapPulse:
        if (now - hold.started < kTapPulseDuration) {
            emitSourceTap(state, source);
            return;
        }
        if (pressed) {
            hold.phase = HoldPhase::Pending;
            hold.started = now;
        } else {
            hold.phase = HoldPhase::Idle;
        }
        return;
    case HoldPhase::WaitForRelease:
        if (!pressed) hold.phase = HoldPhase::Idle;
        return;
    }
}

void TouchpadGestureMapper::transformGhost(DualSenseInputState& state,
                                           Clock::time_point now) noexcept {
    const bool modifierPressed = (state.dpad & 0x08U) != 0;
    if (ghostTapActive_) {
        if (now - ghostTapStarted_ < kTapPulseDuration) {
            state.dpad = static_cast<std::uint8_t>(state.dpad | 0x08U);
        } else {
            ghostTapActive_ = false;
        }
    }

    if (modifierPressed) {
        state.dpad = static_cast<std::uint8_t>(state.dpad & ~0x08U);
        if (!ghostModifierWasPressed_) {
            ghostArmed_ = rightStickNeutral(state);
            ghostFired_ = false;
            ghostTapActive_ = false;
        }
        if (ghostArmed_ && !ghostFired_) {
            if (const auto direction = rightStickDirection(state)) {
                startSwipe(*direction, now);
                ghostFired_ = true;
            }
        }
        if (ghostFired_) {
            state.rx = 0x80;
            state.ry = 0x80;
        }
    } else if (ghostModifierWasPressed_) {
        if (!ghostFired_) {
            ghostTapActive_ = true;
            ghostTapStarted_ = now;
            ++stats_.replayedTaps;
            state.dpad = static_cast<std::uint8_t>(state.dpad | 0x08U);
        }
        ghostArmed_ = false;
        ghostFired_ = false;
    }
    ghostModifierWasPressed_ = modifierPressed;
}

void TouchpadGestureMapper::transformWarframe(DualSenseInputState& state,
                                              Clock::time_point now) noexcept {
    const bool modifierPressed = (state.buttons & button::kR1) != 0;
    const auto face = warframeFace(state.buttons);
    if (warframeTapActive_) {
        if (now - warframeTapStarted_ < kTapPulseDuration) {
            state.buttons = static_cast<std::uint16_t>(state.buttons | button::kR1);
        } else {
            warframeTapActive_ = false;
        }
    }

    // If RB and a face button first appear in the same report, there is no
    // evidence that the user entered the modifier layer. Preserve that whole
    // press unchanged instead of reordering or partially swallowing it.
    if (modifierPressed && !warframeModifierWasPressed_ && face) {
        warframePassthrough_ = true;
        warframeArmed_ = false;
        warframeFired_ = false;
        warframeTapActive_ = false;
    }
    if (warframePassthrough_) {
        if (!modifierPressed) warframePassthrough_ = false;
        warframeModifierWasPressed_ = modifierPressed;
        return;
    }

    if (modifierPressed) {
        state.buttons = static_cast<std::uint16_t>(state.buttons & ~button::kR1);
        if (!warframeModifierWasPressed_) {
            warframeArmed_ = !face.has_value();
            warframeFired_ = false;
            warframeConsumedFace_ = 0;
            warframeTapActive_ = false;
        }
        // Require RB/R1 to have appeared in an earlier input report. This
        // preserves the documented modifier-first ordering and rejects an
        // accidental simultaneous press.
        if (warframeArmed_ && !warframeFired_ &&
            warframeModifierWasPressed_ && face) {
            startSwipe(face->first, now);
            warframeFired_ = true;
            warframeConsumedFace_ = face->second;
        }
        if (warframeFired_) {
            state.buttons = static_cast<std::uint16_t>(
                state.buttons & ~warframeConsumedFace_);
        }
    } else if (warframeModifierWasPressed_) {
        if (!warframeFired_) {
            warframeTapActive_ = true;
            warframeTapStarted_ = now;
            ++stats_.replayedTaps;
            state.buttons = static_cast<std::uint16_t>(state.buttons | button::kR1);
        }
        warframeArmed_ = false;
        warframeFired_ = false;
        warframeConsumedFace_ = 0;
    }
    warframeModifierWasPressed_ = modifierPressed;
}

void TouchpadGestureMapper::transform(DualSenseInputState& state,
                                      Clock::time_point now) noexcept {
    switch (profile_) {
    case TouchpadGestureProfile::None:
        return;
    case TouchpadGestureProfile::LegacyViewHoldSwipeUp:
        transformHold(viewHold_, HoldSource::View, TouchpadSwipeDirection::Up,
                      state, now);
        break;
    case TouchpadGestureProfile::SpiderMan2:
        transformHold(viewHold_, HoldSource::View, TouchpadSwipeDirection::Left,
                      state, now);
        transformHold(dpadUpHold_, HoldSource::DpadUp, TouchpadSwipeDirection::Up,
                      state, now);
        break;
    case TouchpadGestureProfile::MilesMorales:
        transformHold(viewHold_, HoldSource::View, TouchpadSwipeDirection::Left,
                      state, now);
        break;
    case TouchpadGestureProfile::GhostOfTsushima:
        transformGhost(state, now);
        break;
    case TouchpadGestureProfile::Warframe:
        transformWarframe(state, now);
        break;
    }
    emitSwipe(state, now);
}

} // namespace asb::dualsense
