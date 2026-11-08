#include "flydigi/Apex6BatteryRefresher.h"

#include <algorithm>
#include <string>

namespace asb::flydigi {

Apex6BatteryRefresher::Apex6BatteryRefresher(Clock::time_point start,
                                             Apex6BatteryRefresherConfig config) noexcept
    : config_(config), lastActivityAt_(start) {}

void Apex6BatteryRefresher::noteCall(Clock::time_point began) noexcept {
    const auto elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - began).count());
    stats_.maximumCallMicroseconds = (std::max)(stats_.maximumCallMicroseconds, elapsed);
    if (elapsed <= static_cast<std::uint64_t>(config_.slowCall.count())) return;
    ++stats_.slowCalls;
    if (stats_.slowCalls >= config_.maximumSlowCalls &&
        disabled_ == Apex6BatteryRefreshDisabled::None) {
        disabled_ = Apex6BatteryRefreshDisabled::SlowCalls;
        waiting_ = false;
    }
}

void Apex6BatteryRefresher::noteMiss() noexcept {
    if (++consecutiveMisses_ >= config_.maximumMisses &&
        disabled_ == Apex6BatteryRefreshDisabled::None) {
        disabled_ = Apex6BatteryRefreshDisabled::NoAnswer;
        waiting_ = false;
    }
}

std::optional<Apex5Identity> Apex6BatteryRefresher::tick(Apex5Device& device, Clock::time_point now) {
    if (disabled_ != Apex6BatteryRefreshDisabled::None) return std::nullopt;
    std::string error;

    if (!waiting_) {
        if (now - lastActivityAt_ < config_.interval) return std::nullopt;
        lastActivityAt_ = now;
        const auto began = Clock::now();
        const bool requested = device.requestApex6Info(error);
        noteCall(began);
        if (!requested) {
            ++stats_.failures;
            noteMiss();
            return std::nullopt;
        }
        ++stats_.requests;
        waiting_ = disabled_ == Apex6BatteryRefreshDisabled::None;
        deadline_ = now + config_.replyWindow;
        lastPollAt_ = now;
        return std::nullopt;
    }

    const bool expired = now >= deadline_;
    if (!expired && now - lastPollAt_ < config_.pollSpacing) return std::nullopt;
    lastPollAt_ = now;

    std::optional<Apex5Identity> reading;
    const auto began = Clock::now();
    const auto outcome = device.pollApex6Info(config_.readsPerPoll, reading, error);
    noteCall(began);
    switch (outcome) {
    case Apex6InfoPoll::Reply:
        ++stats_.replies;
        consecutiveMisses_ = 0;
        waiting_ = false;
        lastActivityAt_ = now;
        return reading;
    case Apex6InfoPoll::Failed:
        ++stats_.failures;
        waiting_ = false;
        lastActivityAt_ = now;
        noteMiss();
        return std::nullopt;
    case Apex6InfoPoll::Pending:
        if (expired) {
            ++stats_.timeouts;
            waiting_ = false;
            lastActivityAt_ = now;
            noteMiss();
        }
        return std::nullopt;
    }
    return std::nullopt;
}

void Apex6BatteryRefresher::reset(Clock::time_point now) noexcept {
    waiting_ = false;
    lastActivityAt_ = now;
}

} // namespace asb::flydigi
