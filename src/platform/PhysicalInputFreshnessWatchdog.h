#pragma once

#include <chrono>

namespace asb::platform {

class PhysicalInputFreshnessWatchdog {
public:
    using Clock = std::chrono::steady_clock;

    explicit PhysicalInputFreshnessWatchdog(
        Clock::duration maximumSilence,
        Clock::time_point initializedAt = Clock::now()) noexcept
        : maximumSilence_(maximumSilence), lastFreshStateAt_(initializedAt) {}

    void observeFreshState(Clock::time_point observedAt = Clock::now()) noexcept {
        lastFreshStateAt_ = observedAt;
    }

    [[nodiscard]] bool expired(
        Clock::time_point observedAt = Clock::now()) const noexcept {
        return observedAt - lastFreshStateAt_ >= maximumSilence_;
    }

private:
    Clock::duration maximumSilence_;
    Clock::time_point lastFreshStateAt_;
};

} // namespace asb::platform
