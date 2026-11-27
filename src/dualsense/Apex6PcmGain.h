#pragma once

#include "dualsense/Apex6HapticShaping.h"

#include <algorithm>
#include <cstdint>

namespace asb::dualsense {

// Gain 100 is the beta.10 quantizer, bit for bit. Higher gains increase quiet
// PCM with a continuous, memoryless curve g*x/(1+(g-1)*abs(x)). Full-scale
// endpoints remain +/-127, without hard clipping, AGC or cross-channel coupling.
// This is an optional amplitude approximation, not actuator resonance tuning.
[[nodiscard]] constexpr std::int8_t scaleApex6Pcm(
    std::int16_t sample, unsigned gainPercent = 100) noexcept {
    const auto gain = std::clamp(gainPercent, 100U, 200U);
    const std::int64_t fullScale = sample >= 0 ? 32767 : 32768;
    if (gain == 100) {
        return static_cast<std::int8_t>(static_cast<std::int32_t>(sample) * 127 / fullScale);
    }
    const auto magnitude = sample >= 0 ? std::int64_t{sample} : -std::int64_t{sample};
    const auto denominator = fullScale * 100 + (gain - 100) * magnitude;
    return static_cast<std::int8_t>(std::int64_t{sample} * 127 * gain / denominator);
}

[[nodiscard]] inline std::int8_t scaleApex6GripPcm(
    std::int16_t sample, unsigned vibrationStrengthPercent, unsigned pcmGainPercent) noexcept {
    if (vibrationStrengthPercent <= 100) {
        return scaleEffectStrength(scaleApex6Pcm(sample, pcmGainPercent), vibrationStrengthPercent);
    }

    const int fullScale = sample < 0 ? 32768 : 32767;
    const int magnitude = sample < 0 ? -static_cast<int>(sample) : sample;
    const int boosted = boostApex6Magnitude(magnitude, vibrationStrengthPercent, fullScale) * 127 / fullScale;
    const int signedBoosted = sample < 0 ? -boosted : boosted;
    const int pcmGain = scaleApex6Pcm(sample, pcmGainPercent);
    // An explicit PCM gain and the vibration control can both be supplied.
    // Use the stronger result so the two gain stages do not compound.
    return static_cast<std::int8_t>(sample < 0 ? (std::min)(signedBoosted, pcmGain)
                                               : (std::max)(signedBoosted, pcmGain));
}

} // namespace asb::dualsense
