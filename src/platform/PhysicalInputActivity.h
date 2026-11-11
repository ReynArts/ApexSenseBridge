#pragma once

#include "dualsense/DualSenseInput.h"
#include "platform/XInputMapping.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <optional>

namespace asb::platform {

struct PhysicalInputActivityStats {
    std::uint64_t stickChanges = 0;
    std::uint64_t l2Changes = 0;
    std::uint64_t r2Changes = 0;
    std::uint64_t buttonChanges = 0;
    std::uint64_t dpadChanges = 0;
    std::uint64_t imuChanges = 0;
    std::uint64_t l2Oscillations = 0;
    std::uint64_t r2Oscillations = 0;
    std::uint64_t buttonBounces = 0;
    std::uint64_t triggerButtonBounces = 0;
    std::uint16_t bouncedButtons = 0;
    std::uint8_t bouncedDpad = 0;
};

inline void accumulatePhysicalInputActivity(PhysicalInputActivityStats& total,
                                            const PhysicalInputActivityStats& added) noexcept {
    total.stickChanges += added.stickChanges;
    total.l2Changes += added.l2Changes;
    total.r2Changes += added.r2Changes;
    total.buttonChanges += added.buttonChanges;
    total.dpadChanges += added.dpadChanges;
    total.imuChanges += added.imuChanges;
    total.l2Oscillations += added.l2Oscillations;
    total.r2Oscillations += added.r2Oscillations;
    total.buttonBounces += added.buttonBounces;
    total.triggerButtonBounces += added.triggerButtonBounces;
    total.bouncedButtons = static_cast<std::uint16_t>(total.bouncedButtons | added.bouncedButtons);
    total.bouncedDpad = static_cast<std::uint8_t>(total.bouncedDpad | added.bouncedDpad);
}

struct PhysicalTriggerSourceStats {
    std::uint8_t xinputMaxL2 = 0;
    std::uint8_t xinputMaxR2 = 0;
    std::uint8_t mappedMaxL2 = 0;
    std::uint8_t mappedMaxR2 = 0;
    std::uint8_t vendorMaxL2 = 0;
    std::uint8_t vendorMaxR2 = 0;
    std::uint64_t fallbackReports = 0;
    std::uint64_t fallbackSwitches = 0;
    bool fallbackUsed = false;
};

inline void accumulatePhysicalTriggerSourceStats(
    PhysicalTriggerSourceStats& total,
    const PhysicalTriggerSourceStats& added) noexcept {
    total.xinputMaxL2 = (std::max)(total.xinputMaxL2, added.xinputMaxL2);
    total.xinputMaxR2 = (std::max)(total.xinputMaxR2, added.xinputMaxR2);
    total.mappedMaxL2 = (std::max)(total.mappedMaxL2, added.mappedMaxL2);
    total.mappedMaxR2 = (std::max)(total.mappedMaxR2, added.mappedMaxR2);
    total.vendorMaxL2 = (std::max)(total.vendorMaxL2, added.vendorMaxL2);
    total.vendorMaxR2 = (std::max)(total.vendorMaxR2, added.vendorMaxR2);
    total.fallbackReports += added.fallbackReports;
    total.fallbackSwitches += added.fallbackSwitches;
    total.fallbackUsed = total.fallbackUsed || added.fallbackUsed;
}

class TriggerOscillationDetector {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::uint8_t kMinimumSwing = 8;
    static constexpr std::uint8_t kHeldFloor = 128;
    static constexpr std::uint8_t kPressedFloor = xinputButton::kTriggerThreshold;
    static constexpr auto kWindow = std::chrono::milliseconds(100);

    void observe(std::uint8_t value, Clock::time_point now) noexcept {
        if (!extremeAt_) {
            extreme_ = value;
            extremeAt_ = now;
            return;
        }
        if (direction_ == 0) {
            const int moved = static_cast<int>(value) - extreme_;
            if (moved >= kMinimumSwing || -moved >= kMinimumSwing) {
                turn_ = extreme_;
                turnAt_ = extremeAt_;
                direction_ = static_cast<std::int8_t>(moved > 0 ? 1 : -1);
                extreme_ = value;
            }
            extremeAt_ = now;
            return;
        }
        const bool extended = direction_ > 0 ? value >= extreme_ : value <= extreme_;
        if (extended) {
            extreme_ = value;
            extremeAt_ = now;
            return;
        }
        const int retraced = direction_ > 0 ? extreme_ - value : value - extreme_;
        if (retraced < kMinimumSwing) return;
        if (turnAt_ && *extremeAt_ - *turnAt_ <= kWindow &&
            (std::max)(turn_, extreme_) >= kHeldFloor &&
            (std::min)(turn_, extreme_) > kPressedFloor) {
            ++reversals_;
        }
        turn_ = extreme_;
        turnAt_ = extremeAt_;
        direction_ = static_cast<std::int8_t>(-direction_);
        extreme_ = value;
        extremeAt_ = now;
    }

    [[nodiscard]] std::uint64_t reversals() const noexcept { return reversals_; }

private:
    std::uint8_t extreme_ = 0;
    std::optional<Clock::time_point> extremeAt_;
    std::uint8_t turn_ = 0;
    std::optional<Clock::time_point> turnAt_;
    std::int8_t direction_ = 0;
    std::uint64_t reversals_ = 0;
};

class ButtonBounceDetector {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto kWindow = std::chrono::milliseconds(40);

    void observe(std::uint16_t buttons, std::uint8_t dpad, Clock::time_point now) noexcept {
        const auto current = static_cast<std::uint32_t>(buttons) |
                             (static_cast<std::uint32_t>(dpad & 0x0F) << 16U);
        if (!initialized_) {
            previous_ = current;
            initialized_ = true;
            return;
        }
        auto changed = current ^ previous_;
        previous_ = current;
        while (changed != 0) {
            const auto bit = static_cast<unsigned int>(std::countr_zero(changed));
            const auto mask = std::uint32_t{1} << bit;
            changed &= ~mask;
            if ((current & mask) == 0) {
                releasedAt_[bit] = now;
                released_ |= mask;
                continue;
            }
            if ((released_ & mask) == 0 || now - releasedAt_[bit] > kWindow) continue;
            if ((mask & kTriggerButtons) != 0) {
                ++triggerBounces_;
            } else {
                ++bounces_;
            }
            bounced_ |= mask;
        }
    }

    [[nodiscard]] std::uint64_t bounces() const noexcept { return bounces_; }
    [[nodiscard]] std::uint64_t triggerBounces() const noexcept { return triggerBounces_; }
    [[nodiscard]] std::uint16_t bouncedButtons() const noexcept {
        return static_cast<std::uint16_t>(bounced_ & 0xFFFFU);
    }
    [[nodiscard]] std::uint8_t bouncedDpad() const noexcept {
        return static_cast<std::uint8_t>((bounced_ >> 16U) & 0x0FU);
    }

private:
    static constexpr std::uint32_t kTriggerButtons =
        dualsense::button::kL2 | dualsense::button::kR2;

    std::array<Clock::time_point, 20> releasedAt_{};
    std::uint32_t released_ = 0;
    std::uint32_t previous_ = 0;
    std::uint32_t bounced_ = 0;
    std::uint64_t bounces_ = 0;
    std::uint64_t triggerBounces_ = 0;
    bool initialized_ = false;
};

class PhysicalInputActivityTracker {
public:
    using Clock = std::chrono::steady_clock;

    void observe(const dualsense::DualSenseInputState& state, Clock::time_point now) noexcept {
        if (previous_) {
            const auto& previous = *previous_;
            if (previous.lx != state.lx || previous.ly != state.ly ||
                previous.rx != state.rx || previous.ry != state.ry) {
                ++stats_.stickChanges;
            }
            if (previous.l2 != state.l2) ++stats_.l2Changes;
            if (previous.r2 != state.r2) ++stats_.r2Changes;
            if (previous.buttons != state.buttons) ++stats_.buttonChanges;
            if (previous.dpad != state.dpad) ++stats_.dpadChanges;
            if (previous.gyroX != state.gyroX || previous.gyroY != state.gyroY ||
                previous.gyroZ != state.gyroZ || previous.accelX != state.accelX ||
                previous.accelY != state.accelY || previous.accelZ != state.accelZ) {
                ++stats_.imuChanges;
            }
        }
        previous_ = state;
        l2_.observe(state.l2, now);
        r2_.observe(state.r2, now);
        buttons_.observe(state.buttons, state.dpad, now);
    }

    [[nodiscard]] PhysicalInputActivityStats stats() const noexcept {
        auto result = stats_;
        result.l2Oscillations = l2_.reversals();
        result.r2Oscillations = r2_.reversals();
        result.buttonBounces = buttons_.bounces();
        result.triggerButtonBounces = buttons_.triggerBounces();
        result.bouncedButtons = buttons_.bouncedButtons();
        result.bouncedDpad = buttons_.bouncedDpad();
        return result;
    }

private:
    std::optional<dualsense::DualSenseInputState> previous_;
    PhysicalInputActivityStats stats_{};
    TriggerOscillationDetector l2_;
    TriggerOscillationDetector r2_;
    ButtonBounceDetector buttons_;
};

class VendorStreamGapTracker {
public:
    using Clock = std::chrono::steady_clock;

    explicit VendorStreamGapTracker(Clock::duration staleThreshold) noexcept
        : staleThreshold_(staleThreshold) {}

    void observe(Clock::time_point now) noexcept {
        if (last_) {
            const auto gap = now - *last_;
            if (gap > maximumGap_) maximumGap_ = gap;
            if (gap > staleThreshold_) ++staleGaps_;
        }
        last_ = now;
    }

    [[nodiscard]] std::uint64_t maximumGapMilliseconds() const noexcept {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(maximumGap_).count());
    }
    [[nodiscard]] std::uint64_t staleGaps() const noexcept { return staleGaps_; }

private:
    Clock::duration staleThreshold_;
    std::optional<Clock::time_point> last_;
    Clock::duration maximumGap_ = Clock::duration::zero();
    std::uint64_t staleGaps_ = 0;
};

}
