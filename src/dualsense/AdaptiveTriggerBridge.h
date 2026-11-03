#pragma once

#include "dualsense/AdaptiveTriggerTranslation.h"
#include "dualsense/DualSenseFeedback.h"
#include "flydigi/Apex5Device.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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
    std::uint64_t zoneProfiles = 0;
    std::uint64_t zoneTransitions = 0;
    std::uint64_t periodicProfiles = 0;
    std::uint64_t periodicTransitions = 0;
    std::uint64_t zoneLatencySamples = 0;
    std::uint64_t zoneLatencyTotalUs = 0;
    std::uint64_t zoneLatencyMaxUs = 0;
    std::uint64_t periodicLatenessSamples = 0;
    std::uint64_t periodicLatenessTotalUs = 0;
    std::uint64_t periodicLatenessMaxUs = 0;
};

class AdaptiveTriggerBridge {
public:
    explicit AdaptiveTriggerBridge(flydigi::Apex5Device& device, unsigned strengthPercent = 100,
                                   bool zoneEmulation = false);
    ~AdaptiveTriggerBridge();
    AdaptiveTriggerBridge(const AdaptiveTriggerBridge&) = delete;
    AdaptiveTriggerBridge& operator=(const AdaptiveTriggerBridge&) = delete;
    void handle(const DualSenseFeedback& feedback);
    void updateTriggerPositions(std::uint8_t left, std::uint8_t right);
    void stepPeriodic(std::chrono::steady_clock::time_point now);
    void quiesce();
    void setZoneUnits(ZoneUnits units);
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] AdaptiveTriggerBridgeStats stats() const;

private:
    void apply(TriggerSide side, std::uint8_t enableBits,
               const std::array<std::uint8_t, 11>& effect);
    void recordBlock(TriggerSide side, std::uint8_t enableBits,
                     const std::array<std::uint8_t, 11>& effect);
    bool send(const ForceTriggerCommand& command, std::uint8_t dualSenseType);

    struct ArmedBow {
        std::array<std::uint8_t, 11> effect{};
        BowEmulation emulation;
        bool ruptured = false;
    };
    std::array<std::optional<ArmedBow>, 2> armedBow_;

    struct ArmedZones {
        std::array<std::uint8_t, 11> effect{};
        ZoneEmulation emulation;
        ZoneTracker tracker;
        ForceTriggerCommand sent;
    };
    std::array<std::optional<ArmedZones>, 2> armedZones_;

    struct ArmedPeriodic {
        std::array<std::uint8_t, 11> effect{};
        PeriodicEmulation emulation;
        ZoneTracker tracker;
        std::chrono::steady_clock::time_point epoch;
        std::chrono::steady_clock::time_point lastSendAt;
        ForceTriggerCommand sent;
        bool pressed = false;
    };
    std::array<std::optional<ArmedPeriodic>, 2> armedPeriodic_;
    std::thread periodicThread_;
    std::mutex periodicMutex_;
    std::condition_variable periodicSignal_;
    bool periodicStop_ = false;
    std::atomic_bool periodicArmed_{false};
    std::atomic_bool quiesced_{false};
    std::atomic_uint64_t periodicProfiles_{0};
    std::atomic_uint64_t periodicTransitions_{0};
    std::atomic_uint64_t zoneLatencySamples_{0};
    std::atomic_uint64_t zoneLatencyTotalUs_{0};
    std::atomic_uint64_t zoneLatencyMaxUs_{0};
    std::atomic_uint64_t periodicLatenessSamples_{0};
    std::atomic_uint64_t periodicLatenessTotalUs_{0};
    std::atomic_uint64_t periodicLatenessMaxUs_{0};
    void periodicLoop();
    void wakePeriodic();

    std::atomic_bool emulationArmed_{false};
    std::array<std::atomic_uint8_t, 2> lastPosition_{};
    std::atomic_uint64_t zoneProfiles_{0};
    std::atomic_uint64_t zoneTransitions_{0};

    flydigi::Apex5Device& device_;
    unsigned strengthPercent_ = 100;
    bool zoneEmulation_ = false;
    ZoneUnits zoneUnits_{};
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
    std::mutex sendMutex_;
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
