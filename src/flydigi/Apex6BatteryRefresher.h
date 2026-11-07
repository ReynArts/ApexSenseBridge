#pragma once

#include "flydigi/Apex5Device.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace asb::flydigi {

struct Apex6BatteryRefresherConfig {
    std::chrono::milliseconds interval{15000};
    std::chrono::milliseconds replyWindow{300};
    std::chrono::milliseconds pollSpacing{5};
    std::chrono::microseconds slowCall{5000};
    std::size_t readsPerPoll = 8;
    unsigned maximumSlowCalls = 2;
    unsigned maximumMisses = 3;
};

enum class Apex6BatteryRefreshDisabled {
    None,
    SlowCalls,
    NoAnswer,
};

struct Apex6BatteryRefresherStats {
    std::uint64_t requests = 0;
    std::uint64_t replies = 0;
    std::uint64_t timeouts = 0;
    std::uint64_t failures = 0;
    std::uint64_t slowCalls = 0;
    std::uint64_t maximumCallMicroseconds = 0;
};

[[nodiscard]] constexpr std::string_view describeApex6BatteryRefreshDisabled(
    Apex6BatteryRefreshDisabled reason) noexcept {
    switch (reason) {
    case Apex6BatteryRefreshDisabled::SlowCalls: return "a controller call took too long";
    case Apex6BatteryRefreshDisabled::NoAnswer: return "the controller stopped answering";
    case Apex6BatteryRefreshDisabled::None: break;
    }
    return "enabled";
}

class Apex6BatteryRefresher {
public:
    using Clock = std::chrono::steady_clock;

    explicit Apex6BatteryRefresher(Clock::time_point start,
                                   Apex6BatteryRefresherConfig config = {}) noexcept;

    [[nodiscard]] std::optional<Apex5Identity> tick(Apex5Device& device, Clock::time_point now);
    void reset(Clock::time_point now) noexcept;

    [[nodiscard]] bool waitingForReply() const noexcept { return waiting_; }
    [[nodiscard]] Apex6BatteryRefreshDisabled disabledReason() const noexcept { return disabled_; }
    [[nodiscard]] const Apex6BatteryRefresherStats& stats() const noexcept { return stats_; }

private:
    void noteCall(Clock::time_point began) noexcept;
    void noteMiss() noexcept;

    Apex6BatteryRefresherConfig config_;
    Clock::time_point lastActivityAt_;
    Clock::time_point lastPollAt_{};
    Clock::time_point deadline_{};
    bool waiting_ = false;
    unsigned consecutiveMisses_ = 0;
    Apex6BatteryRefreshDisabled disabled_ = Apex6BatteryRefreshDisabled::None;
    Apex6BatteryRefresherStats stats_{};
};

} // namespace asb::flydigi
