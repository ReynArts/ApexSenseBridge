#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

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

// Readiness must be proved by operator states, not command acknowledgements or
// mapped HID traffic. Several samples spanning time reject a single stale burst.
class IndependentTriggerStreamFreshness {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto kMaximumSilence = std::chrono::milliseconds(250);
    static constexpr auto kValidationSpan = std::chrono::milliseconds(20);

    void observe(Clock::time_point now = Clock::now()) noexcept {
        if (!last_ || now - *last_ >= kMaximumSilence) {
            first_ = now;
            samples_ = 0;
        }
        last_ = now;
        if (samples_ < 3) ++samples_;
    }

    [[nodiscard]] bool ready(Clock::time_point now = Clock::now()) const noexcept {
        return first_ && last_ && samples_ >= 3 &&
               *last_ - *first_ >= kValidationSpan &&
               now - *last_ < kMaximumSilence;
    }

private:
    std::optional<Clock::time_point> first_;
    std::optional<Clock::time_point> last_;
    std::uint8_t samples_ = 0;
};

} // namespace asb::platform
