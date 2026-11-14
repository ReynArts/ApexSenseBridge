#pragma once

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
