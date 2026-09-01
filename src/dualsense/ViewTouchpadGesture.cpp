#include "dualsense/ViewTouchpadGesture.h"

#include <algorithm>

namespace asb::dualsense {
namespace {

constexpr std::uint16_t kTouchCenterX = 960;
constexpr std::uint16_t kSwipeStartY = 850;
constexpr std::uint16_t kSwipeEndY = 200;

void clearSyntheticTouch(DualSenseInputState& state) noexcept {
    state.buttons = static_cast<std::uint16_t>(state.buttons & ~button::kTouchpadClick);
    state.touch1X = 0;
    state.touch1Y = 0;
    state.touch1Active = false;
}

} // namespace

void ViewTouchpadGesture::emitTap(DualSenseInputState& state) const noexcept {
    state.buttons = static_cast<std::uint16_t>(state.buttons | button::kTouchpadClick);
    state.touch1X = kTouchCenterX;
    state.touch1Y = 540;
    state.touch1Active = true;
}

void ViewTouchpadGesture::emitSwipe(DualSenseInputState& state,
                                    std::chrono::milliseconds elapsed) const noexcept {
    const auto bounded = std::clamp(elapsed, std::chrono::milliseconds::zero(),
                                    kSwipeDuration);
    const auto travelled = static_cast<std::int64_t>(kSwipeStartY - kSwipeEndY) *
                           bounded.count() / kSwipeDuration.count();
    state.touch1X = kTouchCenterX;
    state.touch1Y = static_cast<std::uint16_t>(kSwipeStartY - travelled);
    state.touch1Active = true;
}

void ViewTouchpadGesture::transform(DualSenseInputState& state,
                                    Clock::time_point now) noexcept {
    const bool viewPressed = (state.buttons & button::kTouchpadClick) != 0;
    clearSyntheticTouch(state);

    switch (phase_) {
    case Phase::Idle:
        if (viewPressed) {
            phase_ = Phase::Pending;
            phaseStarted_ = now;
        }
        return;

    case Phase::Pending:
        if (!viewPressed) {
            phase_ = Phase::TapPulse;
            phaseStarted_ = now;
            ++stats_.taps;
            emitTap(state);
            return;
        }
        if (now - phaseStarted_ >= kHoldThreshold) {
            phase_ = Phase::Swipe;
            phaseStarted_ = now;
            ++stats_.swipes;
            emitSwipe(state, std::chrono::milliseconds::zero());
        }
        return;

    case Phase::TapPulse:
        if (now - phaseStarted_ < kTapPulseDuration) {
            emitTap(state);
            return;
        }
        if (viewPressed) {
            phase_ = Phase::Pending;
            phaseStarted_ = now;
        } else {
            phase_ = Phase::Idle;
        }
        return;

    case Phase::Swipe: {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - phaseStarted_);
        if (elapsed < kSwipeDuration) {
            emitSwipe(state, elapsed);
            return;
        }
        phase_ = viewPressed ? Phase::WaitForRelease : Phase::Idle;
        return;
    }

    case Phase::WaitForRelease:
        if (!viewPressed) phase_ = Phase::Idle;
        return;
    }
}

} // namespace asb::dualsense
