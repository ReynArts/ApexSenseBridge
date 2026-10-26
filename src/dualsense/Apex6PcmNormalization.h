#pragma once

#include "dualsense/Apex6PcmGain.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace asb::dualsense {

enum class Apex6PcmMode : std::uint8_t {
    Original = 0,
    Companding = 1,
    Linear = 2,
    Normalize = 3
};

[[nodiscard]] constexpr std::string_view apex6PcmModeName(Apex6PcmMode mode) noexcept {
    switch (mode) {
    case Apex6PcmMode::Original: return "original";
    case Apex6PcmMode::Companding: return "companding";
    case Apex6PcmMode::Linear: return "linear";
    case Apex6PcmMode::Normalize: return "normalize";
    }
    return "original";
}

[[nodiscard]] inline std::optional<Apex6PcmMode> parseApex6PcmMode(std::string_view text) noexcept {
    if (text == "original") return Apex6PcmMode::Original;
    if (text == "companding") return Apex6PcmMode::Companding;
    if (text == "linear") return Apex6PcmMode::Linear;
    if (text == "normalize") return Apex6PcmMode::Normalize;
    return std::nullopt;
}

struct Apex6PcmConfig {
    Apex6PcmMode mode = Apex6PcmMode::Original;
    unsigned linearGainPercent = 100;
    unsigned referencePercent = 0;
    unsigned referencePeakAbs16 = 0;
    unsigned targetPercent = 90;
    unsigned maxGainPercent = 400;
    unsigned legacyGainPercent = 100;
};

struct Apex6PcmScalingState {
    double requestedGainRatio = 1.0;
    double effectiveGainRatio = 1.0;
    bool gainLimited = false;
    bool valid = true;
    std::string status = "valid";
};

class Apex6PcmScaler {
public:
    explicit Apex6PcmScaler(const Apex6PcmConfig& config = {}) noexcept
        : config_(config) {
        init();
    }

    [[nodiscard]] std::int8_t scaleSample(std::int16_t sample, bool& clipped) const noexcept {
        clipped = false;
        const std::int64_t fullScale = sample >= 0 ? 32767 : 32768;

        switch (config_.mode) {
        case Apex6PcmMode::Original:
            return static_cast<std::int8_t>(static_cast<std::int32_t>(sample) * 127 / fullScale);

        case Apex6PcmMode::Companding:
            return scaleApex6Pcm(sample, config_.legacyGainPercent);

        case Apex6PcmMode::Linear:
        case Apex6PcmMode::Normalize: {
            if (!state_.valid || state_.effectiveGainRatio == 1.0) {
                return static_cast<std::int8_t>(static_cast<std::int32_t>(sample) * 127 / fullScale);
            }
            const double originalOut = static_cast<double>(sample) * 127.0 / static_cast<double>(fullScale);
            const double scaledOut = originalOut * state_.effectiveGainRatio;
            const auto rounded = static_cast<std::int64_t>(std::lround(scaledOut));
            if (rounded > 127) {
                clipped = true;
                return 127;
            }
            if (rounded < -127) {
                clipped = true;
                return -127;
            }
            return static_cast<std::int8_t>(rounded);
        }
        }
        return static_cast<std::int8_t>(static_cast<std::int32_t>(sample) * 127 / fullScale);
    }

    [[nodiscard]] const Apex6PcmScalingState& state() const noexcept { return state_; }
    [[nodiscard]] const Apex6PcmConfig& config() const noexcept { return config_; }

private:
    void init() noexcept {
        state_ = {};
        if (config_.mode == Apex6PcmMode::Original) {
            state_.requestedGainRatio = 1.0;
            state_.effectiveGainRatio = 1.0;
            state_.gainLimited = false;
            state_.valid = true;
            state_.status = "valid";
            return;
        }

        if (config_.mode == Apex6PcmMode::Companding) {
            const auto gain = std::clamp(config_.legacyGainPercent, 100U, 200U);
            state_.requestedGainRatio = static_cast<double>(gain) / 100.0;
            state_.effectiveGainRatio = state_.requestedGainRatio;
            state_.gainLimited = false;
            state_.valid = true;
            state_.status = "valid";
            return;
        }

        if (config_.mode == Apex6PcmMode::Linear) {
            const auto gain = std::clamp(config_.linearGainPercent, 0U, 400U);
            state_.requestedGainRatio = static_cast<double>(gain) / 100.0;
            state_.effectiveGainRatio = state_.requestedGainRatio;
            state_.gainLimited = false;
            state_.valid = true;
            state_.status = "valid";
            return;
        }

        if (config_.mode == Apex6PcmMode::Normalize) {
            double refPct = 0.0;
            if (config_.referencePeakAbs16 > 0) {
                refPct = static_cast<double>(config_.referencePeakAbs16) / 327.67;
            } else if (config_.referencePercent > 0) {
                refPct = static_cast<double>(config_.referencePercent);
            }

            if (refPct <= 0.0) {
                state_.requestedGainRatio = 1.0;
                state_.effectiveGainRatio = 1.0;
                state_.gainLimited = false;
                state_.valid = false;
                state_.status = "invalid";
                return;
            }

            const double targetPct = static_cast<double>(std::clamp(config_.targetPercent, 1U, 100U));
            const double requestedGain = targetPct / refPct;
            state_.requestedGainRatio = requestedGain;

            const double maxGain = static_cast<double>(std::clamp(config_.maxGainPercent, 100U, 400U)) / 100.0;
            if (requestedGain > maxGain) {
                state_.effectiveGainRatio = maxGain;
                state_.gainLimited = true;
                state_.valid = true;
                state_.status = "limited";
            } else {
                state_.effectiveGainRatio = (std::max)(0.0, requestedGain);
                state_.gainLimited = false;
                state_.valid = true;
                state_.status = "valid";
            }
        }
    }

    Apex6PcmConfig config_{};
    Apex6PcmScalingState state_{};
};

} // namespace asb::dualsense
