#pragma once

#include "dualsense/DualSenseFeedback.h"
#include "flydigi/Apex5Device.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
    std::uint8_t lastUnsupportedLeftDualSenseType = 0;
    std::uint8_t lastUnsupportedRightDualSenseType = 0;
    std::vector<std::pair<std::uint8_t, std::uint64_t>> unsupportedByType;
    std::vector<std::string> leftBlocks;
    std::vector<std::string> rightBlocks;
};

class AdaptiveTriggerBridge {
public:
    explicit AdaptiveTriggerBridge(flydigi::Apex5Device& device, unsigned strengthPercent = 100);
    void handle(const DualSenseFeedback& feedback);
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] AdaptiveTriggerBridgeStats stats() const;

private:
    void apply(TriggerSide side, std::uint8_t enableBits,
               const std::array<std::uint8_t, 11>& effect);
    void recordBlock(TriggerSide side, std::uint8_t enableBits,
                     const std::array<std::uint8_t, 11>& effect);

    flydigi::Apex5Device& device_;
    unsigned strengthPercent_ = 100;
    std::optional<ForceTriggerCommand> lastLeft_;
    std::optional<ForceTriggerCommand> lastRight_;
    std::uint8_t lastActiveLeftType_ = 0;
    std::uint8_t lastActiveRightType_ = 0;
    std::optional<ForceTriggerCommand> lastActiveLeft_;
    std::optional<ForceTriggerCommand> lastActiveRight_;
    std::array<std::array<std::uint8_t, 12>, 2> lastBlock_{};
    std::array<std::vector<std::array<std::uint8_t, 12>>, 2> seenBlocks_;
    std::array<std::vector<std::string>, 2> blockLog_;
    std::array<std::uint64_t, 256> unsupportedByType_{};
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
    std::atomic_uint8_t lastUnsupportedLeftType_{0};
    std::atomic_uint8_t lastUnsupportedRightType_{0};
};

} // namespace asb::dualsense
