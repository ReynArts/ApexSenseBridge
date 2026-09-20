#pragma once

#include "dualsense/DualSenseFeedback.h"
#include "dualsense/DualSenseInput.h"
#include "flydigi/Apex5Device.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace asb::dualsense {

struct AdaptiveTriggerBridgeStats {
    std::uint64_t translated = 0;
    std::uint64_t active = 0;
    std::uint64_t normal = 0;
    std::uint64_t deduplicated = 0;
    std::uint64_t neutral = 0;
    std::uint64_t unsupported = 0;
    std::uint64_t writeFailures = 0;
    std::uint8_t lastLeftDualSenseType = 0;
    std::uint8_t lastRightDualSenseType = 0;
    std::optional<ForceTriggerCommand> lastLeftCommand;
    std::optional<ForceTriggerCommand> lastRightCommand;
    std::uint8_t lastActiveLeftDualSenseType = 0;
    std::uint8_t lastActiveRightDualSenseType = 0;
    std::optional<ForceTriggerCommand> lastActiveLeftCommand;
    std::optional<ForceTriggerCommand> lastActiveRightCommand;
};

class AdaptiveTriggerBridge {
public:
    explicit AdaptiveTriggerBridge(flydigi::Apex5Device& device);
    void handle(const DualSenseFeedback& feedback);

    // Records that something outside this bridge has just put both triggers
    // back to Normal - the startup baseline, or the reset that follows an
    // Apex 5 profile switch. Without it the bridge starts out believing it
    // knows nothing, and writes Normal over Normal the first time a game
    // releases a trigger; on the dongle that redundant write costs about a
    // second of frozen input. It also keeps the cache honest after a reset
    // the bridge did not perform, which would otherwise suppress a write the
    // game genuinely needs. Effects already held are kept: a baseline changes
    // what the pad is doing, not what the game asked for.
    void noteNormalBaseline();
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] AdaptiveTriggerBridgeStats stats() const noexcept;

private:
    void apply(TriggerSide side, const std::array<std::uint8_t, 11>& effect);
    bool writeNow(TriggerSide side, const ForceTriggerCommand& command,
                  std::uint8_t dualSenseType);

    flydigi::Apex5Device& device_;
    std::uint8_t leftMotor_ = 0;
    std::optional<ForceTriggerCommand> lastLeft_;
    std::optional<ForceTriggerCommand> lastRight_;
    std::uint8_t lastActiveLeftType_ = 0;
    std::uint8_t lastActiveRightType_ = 0;
    std::optional<ForceTriggerCommand> lastActiveLeft_;
    std::optional<ForceTriggerCommand> lastActiveRight_;
    mutable std::mutex stateMutex_;
    std::atomic_bool failed_{false};
    mutable std::mutex errorMutex_;
    std::string error_;
    std::atomic_uint64_t translated_{0};
    std::atomic_uint64_t active_{0};
    std::atomic_uint64_t normal_{0};
    std::atomic_uint64_t deduplicated_{0};
    std::atomic_uint64_t neutral_{0};
    std::atomic_uint64_t unsupported_{0};
    std::atomic_uint64_t writeFailures_{0};
    std::atomic_uint8_t lastLeftType_{0};
    std::atomic_uint8_t lastRightType_{0};
};

} // namespace asb::dualsense
