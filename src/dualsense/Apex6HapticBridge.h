#pragma once

#include "core/TriggerEffect.h"
#include "dualsense/DualSenseFeedback.h"
#include "flydigi/Apex5Device.h"
#include "flydigi/Apex6Protocol.h"
#include "haptics/HapticProcessor.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace asb::dualsense {

struct Apex6HapticBridgeStats {
    std::uint64_t framesWritten = 0;
    std::uint64_t waveformBlocks = 0;
    std::uint64_t waveformBlocksRendered = 0;
    std::uint64_t waveformBlocksDropped = 0;
    std::uint64_t waveformQueueMaxDepth = 0;
    std::uint64_t waveformSequenceGaps = 0;
    std::uint64_t waveformDuplicates = 0;
    std::uint64_t waveformOutOfOrder = 0;
    std::uint64_t waveformUnderruns = 0;
    std::uint64_t waveformOverflowDrops = 0;
    std::uint64_t deadlineOverruns = 0;
    std::uint64_t writeFailures = 0;
    std::uint64_t totalWriteDurationUs = 0;
    std::uint64_t maximumWriteDurationUs = 0;
    std::uint64_t leftTriggerFrames = 0;
    std::uint64_t rightTriggerFrames = 0;
    std::uint64_t bothTriggerFrames = 0;
};

class Apex6HapticBridge {
public:
    explicit Apex6HapticBridge(flydigi::Apex5Device& device,
                               haptics::HapticConfig config = {},
                               bool routeGrips = true);
    ~Apex6HapticBridge();

    Apex6HapticBridge(const Apex6HapticBridge&) = delete;
    Apex6HapticBridge& operator=(const Apex6HapticBridge&) = delete;

    bool start(std::string& error);
    bool stop(std::string& error) noexcept;
    void handle(const DualSenseFeedback& feedback);
    void updateTriggerPositions(std::uint8_t left, std::uint8_t right) noexcept;
    void setDiagnosticTrigger(const ForceTriggerCommand& command) noexcept;
    void setDiagnosticGrips(std::uint8_t left, std::uint8_t right) noexcept;

    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] Apex6HapticBridgeStats stats() const noexcept;

private:
    using Clock = std::chrono::steady_clock;

    struct WaveformBlock {
        std::uint32_t sequence = 0;
        std::array<std::int16_t, 8> left{};
        std::array<std::int16_t, 8> right{};
    };

    static constexpr std::size_t kWaveformQueueCapacity = 3;

    void run() noexcept;
    flydigi::apex6::MotorBlock renderBlock(
        flydigi::apex6::TriggerRoute& route,
        bool& triggerEnabled,
        Clock::time_point now) noexcept;
    std::array<std::int8_t, 8> renderTrigger(
        const std::optional<ForceTriggerCommand>& command,
        std::uint8_t position,
        double& phase) noexcept;
    void recordError(std::string error) noexcept;

    flydigi::Apex5Device& device_;
    haptics::HapticProcessor hapticProcessor_;
    bool routeGrips_ = true;
    mutable std::mutex stateMutex_;
    std::condition_variable stopSignal_;
    std::thread worker_;
    bool stopping_ = false;
    bool started_ = false;

    std::deque<WaveformBlock> waveformQueue_;
    std::optional<std::uint32_t> lastWaveformSequence_;
    Clock::time_point lastWaveformAt_{};
    haptics::MotorLevels audioEnvelope_{};
    Clock::time_point lastAudioEnvelopeAt_{};
    std::uint8_t rumbleLow_ = 0;
    std::uint8_t rumbleHigh_ = 0;
    bool diagnosticGripCarrier_ = false;
    std::optional<ForceTriggerCommand> leftTrigger_;
    std::optional<ForceTriggerCommand> rightTrigger_;
    std::uint8_t leftPosition_ = 0;
    std::uint8_t rightPosition_ = 0;
    bool routeRightNext_ = false;
    double leftGripPhase_ = 0.0;
    double rightGripPhase_ = 0.0;
    double leftTriggerPhase_ = 0.0;
    double rightTriggerPhase_ = 0.0;

    mutable std::mutex errorMutex_;
    std::string error_;
    std::atomic_bool failed_{false};
    std::atomic_uint64_t framesWritten_{0};
    std::atomic_uint64_t waveformBlocks_{0};
    std::atomic_uint64_t waveformBlocksRendered_{0};
    std::atomic_uint64_t waveformBlocksDropped_{0};
    std::atomic_uint64_t waveformQueueMaxDepth_{0};
    std::atomic_uint64_t waveformSequenceGaps_{0};
    std::atomic_uint64_t waveformDuplicates_{0};
    std::atomic_uint64_t waveformOutOfOrder_{0};
    std::atomic_uint64_t waveformUnderruns_{0};
    std::atomic_uint64_t waveformOverflowDrops_{0};
    std::atomic_uint64_t deadlineOverruns_{0};
    std::atomic_uint64_t writeFailures_{0};
    std::atomic_uint64_t totalWriteDurationUs_{0};
    std::atomic_uint64_t maximumWriteDurationUs_{0};
    std::atomic_uint64_t leftTriggerFrames_{0};
    std::atomic_uint64_t rightTriggerFrames_{0};
    std::atomic_uint64_t bothTriggerFrames_{0};
};

} // namespace asb::dualsense
