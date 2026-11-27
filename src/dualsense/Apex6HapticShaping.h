#pragma once

#include "dualsense/EffectStrength.h"

#include <algorithm>
#include <cstdint>

namespace asb::dualsense {

inline constexpr unsigned kApex6GripDuckStartPosition = 20;
inline constexpr unsigned kApex6GripDuckFullPosition = 100;
inline constexpr unsigned kApex6TriggerCeilingMinimumPercent = 20;
inline constexpr unsigned kApex6GripNoDuckPermille = 1000;

[[nodiscard]] constexpr unsigned apex6GripDuckGainPermille(
    std::uint8_t leftPosition, std::uint8_t rightPosition, unsigned duckPercent) noexcept {
    const auto duck = (std::min)(duckPercent, 100U);
    if (duck == 0) return kApex6GripNoDuckPermille;
    const unsigned position = (std::max)(leftPosition, rightPosition);
    if (position <= kApex6GripDuckStartPosition) return kApex6GripNoDuckPermille;
    constexpr unsigned span = kApex6GripDuckFullPosition - kApex6GripDuckStartPosition;
    const unsigned travel = (std::min)(position - kApex6GripDuckStartPosition, span);
    return kApex6GripNoDuckPermille - duck * 10U * travel / span;
}

[[nodiscard]] constexpr std::int8_t applyApex6GripDuck(
    std::int8_t sample, unsigned gainPermille) noexcept {
    if (gainPermille >= kApex6GripNoDuckPermille) return sample;
    return static_cast<std::int8_t>(
        static_cast<std::int32_t>(sample) * static_cast<std::int32_t>(gainPermille) / 1000);
}

[[nodiscard]] constexpr int boostApex6Magnitude(
    int magnitude, unsigned percent, int ceiling) noexcept {
    if (magnitude >= ceiling || percent <= 100) return magnitude;

    // Apply the requested gain up to 75% of full scale. A quadratic knee
    // then tapers it into the ceiling, with matching slopes at both ends.
    const int amplified = magnitude * static_cast<int>((std::min)(percent, 200U));
    if (amplified <= ceiling * 75) return amplified / 100;
    const std::int64_t remaining = ceiling * 125 - amplified;
    if (remaining <= 0) return ceiling;
    const std::int64_t divisor = ceiling * 10000;
    const int limited = ceiling - static_cast<int>((remaining * remaining + divisor - 1) / divisor);
    return (std::max)(magnitude, limited);
}

[[nodiscard]] inline std::int8_t scaleApex6ActuatorStrength(
    std::int8_t sample, unsigned percent, int ceiling) noexcept {
    if (percent <= 100) return scaleEffectStrength(sample, percent);
    const int magnitude = sample < 0 ? -static_cast<int>(sample) : sample;
    const int boosted = boostApex6Magnitude(magnitude, percent, ceiling);
    return static_cast<std::int8_t>(sample < 0 ? -boosted : boosted);
}

[[nodiscard]] constexpr std::int8_t limitApex6TriggerSample(
    std::int8_t sample, unsigned ceilingPercent) noexcept {
    const auto percent = std::clamp(ceilingPercent, kApex6TriggerCeilingMinimumPercent, 100U);
    if (percent >= 100U || sample == 0) return sample;
    const double ceiling = 127.0 * static_cast<double>(percent) / 100.0;
    const double knee = ceiling * 0.5;
    const double magnitude = sample < 0
        ? -static_cast<double>(sample) : static_cast<double>(sample);
    if (magnitude <= knee) return sample;
    const double excess = (magnitude - knee) / (ceiling - knee);
    const double limited = knee + (ceiling - knee) * excess / (1.0 + excess);
    const auto rounded = (std::min)(static_cast<std::int32_t>(limited + 0.5),
                                    static_cast<std::int32_t>(ceiling + 0.5));
    return static_cast<std::int8_t>(sample < 0 ? -rounded : rounded);
}

} // namespace asb::dualsense
