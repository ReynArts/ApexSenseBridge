#pragma once

#include "dualsense/DualSenseFeedback.h"
#include "flydigi/Apex5Device.h"
#include "haptics/HapticProcessor.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace asb::dualsense {

using RumbleLevels = haptics::MotorLevels;

struct RumbleBridgeStats {
    std::uint64_t updates = 0;
    std::uint64_t writes = 0;
    std::uint64_t stops = 0;
    std::uint64_t deduplicated = 0;
    std::uint64_t writeFailures = 0;
    std::uint64_t audioFrames = 0;
    std::uint64_t audioActiveFrames = 0;
    std::uint64_t audioRateLimited = 0;
    std::uint64_t audioTimeouts = 0;
    std::uint64_t audioLowFrames = 0;
    std::uint64_t audioMediumFrames = 0;
    std::uint64_t audioHighFrames = 0;
    std::uint16_t maximumLeftEnergy = 0;
    std::uint16_t maximumRightEnergy = 0;
    std::uint16_t maximumLeftPeak = 0;
    std::uint16_t maximumRightPeak = 0;
    std::uint16_t maximumLeftTransient = 0;
    std::uint16_t maximumRightTransient = 0;
    std::uint8_t lastAudioLowFrequency = 0;
    std::uint8_t lastAudioHighFrequency = 0;
    std::uint8_t lastLowFrequency = 0;
    std::uint8_t lastHighFrequency = 0;
};

class RumbleBridge {
public:
    explicit RumbleBridge(flydigi::Apex5Device& device,
                          haptics::HapticConfig hapticConfig = {});

    void handle(const DualSenseFeedback& feedback);
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] RumbleBridgeStats stats() const noexcept;

private:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] RumbleLevels mixedLevelsLocked() const noexcept;
    void writeDesiredLocked(RumbleLevels desired,
                            Clock::time_point now,
                            bool force,
                            bool audioOrigin);

    flydigi::Apex5Device& device_;
    haptics::HapticProcessor hapticProcessor_;
    RumbleLevels standard_{};
    RumbleLevels audio_{};
    std::optional<RumbleLevels> previous_{RumbleLevels{}};
    Clock::time_point lastWriteAt_{Clock::now()};
    Clock::time_point lastAudioAt_{};
    bool hasAudioTimestamp_ = false;
    bool pendingOutput_ = false;
    mutable std::mutex stateMutex_;
    mutable std::mutex errorMutex_;
    std::string error_;
    std::atomic_bool failed_{false};
    std::atomic_uint64_t updates_{0};
    std::atomic_uint64_t writes_{0};
    std::atomic_uint64_t stops_{0};
    std::atomic_uint64_t deduplicated_{0};
    std::atomic_uint64_t writeFailures_{0};
    std::atomic_uint64_t audioFrames_{0};
    std::atomic_uint64_t audioActiveFrames_{0};
    std::atomic_uint64_t audioRateLimited_{0};
    std::atomic_uint64_t audioTimeouts_{0};
    std::atomic_uint64_t audioLowFrames_{0};
    std::atomic_uint64_t audioMediumFrames_{0};
    std::atomic_uint64_t audioHighFrames_{0};
    std::uint16_t maximumLeftEnergy_ = 0;
    std::uint16_t maximumRightEnergy_ = 0;
    std::uint16_t maximumLeftPeak_ = 0;
    std::uint16_t maximumRightPeak_ = 0;
    std::uint16_t maximumLeftTransient_ = 0;
    std::uint16_t maximumRightTransient_ = 0;
};

} // namespace asb::dualsense
