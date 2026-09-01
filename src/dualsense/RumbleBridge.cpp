#include "dualsense/RumbleBridge.h"

#include <algorithm>

namespace asb::dualsense {
namespace {

bool isZero(const RumbleLevels& levels) noexcept {
    return levels.lowFrequency == 0 && levels.highFrequency == 0;
}

unsigned difference(std::uint8_t left, std::uint8_t right) noexcept {
    return left > right ? static_cast<unsigned>(left - right)
                        : static_cast<unsigned>(right - left);
}

} // namespace

RumbleBridge::RumbleBridge(flydigi::Apex5Device& device,
                           haptics::HapticConfig hapticConfig)
    : device_(device), hapticProcessor_(hapticConfig) {}

void RumbleBridge::handle(const DualSenseFeedback& feedback) {
    if (failed_.load(std::memory_order_relaxed)) {
        return;
    }

    const auto now = Clock::now();
    std::lock_guard lock(stateMutex_);
    bool stateChanged = false;
    bool force = false;
    bool audioOrigin = false;

    if (feedback.kind == FeedbackKind::AudioHaptics) {
        audioFrames_.fetch_add(1, std::memory_order_relaxed);
        maximumLeftEnergy_ = (std::max)(maximumLeftEnergy_, feedback.leftEnergy);
        maximumRightEnergy_ = (std::max)(maximumRightEnergy_, feedback.rightEnergy);
        maximumLeftPeak_ = (std::max)(maximumLeftPeak_, feedback.leftPeak);
        maximumRightPeak_ = (std::max)(maximumRightPeak_, feedback.rightPeak);
        maximumLeftTransient_ = (std::max)(maximumLeftTransient_, feedback.leftTransient);
        maximumRightTransient_ = (std::max)(maximumRightTransient_, feedback.rightTransient);

        const auto next = hapticProcessor_.process(feedback);
        if (!isZero(next)) {
            audioActiveFrames_.fetch_add(1, std::memory_order_relaxed);
            const auto strength = (std::max)(next.lowFrequency, next.highFrequency);
            if (strength <= 31) {
                audioLowFrames_.fetch_add(1, std::memory_order_relaxed);
            } else if (strength <= 95) {
                audioMediumFrames_.fetch_add(1, std::memory_order_relaxed);
            } else {
                audioHighFrames_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        stateChanged = next != audio_;
        audio_ = next;
        lastAudioAt_ = now;
        hasAudioTimestamp_ = true;
        audioOrigin = true;
    } else {
        constexpr auto kAudioStreamTimeout = std::chrono::milliseconds(100);
        if (hasAudioTimestamp_ && !isZero(audio_) &&
            now - lastAudioAt_ >= kAudioStreamTimeout) {
            audio_ = {};
            stateChanged = true;
            force = true;
            audioOrigin = true;
            audioTimeouts_.fetch_add(1, std::memory_order_relaxed);
        }

        if (feedback.requestsRumbleUpdate()) {
            updates_.fetch_add(1, std::memory_order_relaxed);
            const RumbleLevels next{feedback.rumbleLeft, feedback.rumbleRight};
            stateChanged = stateChanged || next != standard_;
            standard_ = next;
            // Standard game-rumble commands are sparse and latency-sensitive.
            force = true;
        }
    }

    if (!stateChanged && !pendingOutput_) {
        if (feedback.kind == FeedbackKind::AudioHaptics ||
            feedback.requestsRumbleUpdate()) {
            deduplicated_.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }
    writeDesiredLocked(mixedLevelsLocked(), now, force, audioOrigin);
}

RumbleLevels RumbleBridge::mixedLevelsLocked() const noexcept {
    return {
        (std::max)(standard_.lowFrequency, audio_.lowFrequency),
        (std::max)(standard_.highFrequency, audio_.highFrequency),
    };
}

void RumbleBridge::writeDesiredLocked(RumbleLevels desired,
                                      Clock::time_point now,
                                      bool force,
                                      bool audioOrigin) {
    if (previous_ && *previous_ == desired) {
        pendingOutput_ = false;
        deduplicated_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const auto previous = previous_.value_or(RumbleLevels{});
    const bool zeroTransition = isZero(previous) != isZero(desired);
    if (!force && !zeroTransition) {
        constexpr auto kMinimumWriteInterval = std::chrono::milliseconds(5);
        constexpr auto kSmallChangeRefresh = std::chrono::milliseconds(25);
        constexpr unsigned kMinimumUsefulDelta = 3;
        const bool tooSoon = now - lastWriteAt_ < kMinimumWriteInterval;
        const bool smallChange =
            difference(previous.lowFrequency, desired.lowFrequency) < kMinimumUsefulDelta &&
            difference(previous.highFrequency, desired.highFrequency) < kMinimumUsefulDelta;
        if (tooSoon || (smallChange && now - lastWriteAt_ < kSmallChangeRefresh)) {
            pendingOutput_ = true;
            if (audioOrigin) {
                audioRateLimited_.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }
    }

    std::string writeError;
    if (!device_.setRumble(desired.lowFrequency, desired.highFrequency, writeError)) {
        writeFailures_.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard lock(errorMutex_);
            error_ = std::move(writeError);
        }
        failed_.store(true, std::memory_order_relaxed);
        return;
    }

    previous_ = desired;
    lastWriteAt_ = now;
    pendingOutput_ = false;
    writes_.fetch_add(1, std::memory_order_relaxed);
    if (isZero(desired)) {
        stops_.fetch_add(1, std::memory_order_relaxed);
    }
}

bool RumbleBridge::failed() const noexcept {
    return failed_.load(std::memory_order_relaxed);
}

std::string RumbleBridge::error() const {
    std::lock_guard lock(errorMutex_);
    return error_;
}

RumbleBridgeStats RumbleBridge::stats() const noexcept {
    std::lock_guard lock(stateMutex_);
    const auto last = previous_.value_or(RumbleLevels{});
    return {updates_.load(std::memory_order_relaxed),
            writes_.load(std::memory_order_relaxed),
            stops_.load(std::memory_order_relaxed),
            deduplicated_.load(std::memory_order_relaxed),
            writeFailures_.load(std::memory_order_relaxed),
            audioFrames_.load(std::memory_order_relaxed),
            audioActiveFrames_.load(std::memory_order_relaxed),
            audioRateLimited_.load(std::memory_order_relaxed),
            audioTimeouts_.load(std::memory_order_relaxed),
            audioLowFrames_.load(std::memory_order_relaxed),
            audioMediumFrames_.load(std::memory_order_relaxed),
            audioHighFrames_.load(std::memory_order_relaxed),
            maximumLeftEnergy_,
            maximumRightEnergy_,
            maximumLeftPeak_,
            maximumRightPeak_,
            maximumLeftTransient_,
            maximumRightTransient_,
            audio_.lowFrequency,
            audio_.highFrequency,
            last.lowFrequency,
            last.highFrequency};
}

} // namespace asb::dualsense
