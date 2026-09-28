#pragma once

#include "dualsense/DualSenseInput.h"
#include "platform/PhysicalInputSource.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace asb::cli {

class MicrosecondLatencyHistogram {
public:
    void observe(std::chrono::steady_clock::duration duration) noexcept {
        const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
            duration).count();
        const auto bucket = static_cast<std::size_t>((std::clamp)(
            microseconds, std::int64_t{0},
            static_cast<std::int64_t>(buckets_.size() - 1)));
        ++buckets_[bucket];
        ++samples_;
    }

    [[nodiscard]] std::uint64_t percentile(unsigned int percentage) const noexcept {
        if (samples_ == 0) return 0;
        const auto wanted = (samples_ * percentage + 99) / 100;
        std::uint64_t cumulative = 0;
        for (std::size_t index = 0; index < buckets_.size(); ++index) {
            cumulative += buckets_[index];
            if (cumulative >= wanted) return index;
        }
        return buckets_.size() - 1;
    }
    [[nodiscard]] std::uint64_t samples() const noexcept { return samples_; }

private:
    // The final bucket includes every value >= 2 ms. The acceptance target is
    // 1.5 ms, so this fixed 16 KiB structure gives useful resolution without
    // allocating or sorting samples in the hot input path.
    std::array<std::uint64_t, 2001> buckets_{};
    std::uint64_t samples_ = 0;
};

struct ProcessUsageSnapshot {
    std::uint64_t cpu100ns = 0;
    std::uint64_t workingSetBytes = 0;
    std::uint64_t peakWorkingSetBytes = 0;
};

[[nodiscard]] ProcessUsageSnapshot processUsageSnapshot() noexcept;
[[nodiscard]] unsigned int logicalProcessorCount() noexcept;
[[nodiscard]] bool gameplayControlsReleased(
    const asb::dualsense::DualSenseInputState& state) noexcept;
[[nodiscard]] bool waitForPhysicalControlsReleased(
    asb::platform::PhysicalInputSource& input,
    std::chrono::milliseconds maximumWait) noexcept;

class ButtonHoldTracker {
public:
    using Clock = std::chrono::steady_clock;

    void observe(bool pressed, Clock::time_point now) noexcept {
        if (pressed) {
            if (!pressedAt_) {
                pressedAt_ = now;
                ++presses_;
            }
            updateMaximum(now);
            return;
        }
        finish(now);
    }

    void finish(Clock::time_point now) noexcept {
        if (!pressedAt_) return;
        updateMaximum(now);
        pressedAt_.reset();
    }
    [[nodiscard]] std::uint64_t presses() const noexcept { return presses_; }
    [[nodiscard]] std::int64_t maximumHoldMilliseconds() const noexcept {
        return maximumHold_.count();
    }

private:
    void updateMaximum(Clock::time_point now) noexcept {
        if (!pressedAt_) return;
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - *pressedAt_);
        if (duration > maximumHold_) maximumHold_ = duration;
    }

    std::optional<Clock::time_point> pressedAt_;
    std::chrono::milliseconds maximumHold_{};
    std::uint64_t presses_ = 0;
};

} // namespace asb::cli
