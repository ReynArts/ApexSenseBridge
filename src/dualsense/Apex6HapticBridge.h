#pragma once

#include "core/TriggerEffect.h"
#include "dualsense/Apex6TriggerEffect.h"
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
    std::uint64_t hidReports = 0;
    std::uint64_t triggerLeftUpdates = 0;
    std::uint64_t triggerRightUpdates = 0;
    std::uint64_t triggerActiveUpdates = 0;
    std::uint64_t triggerStops = 0;
    std::uint64_t triggerUnsupported = 0;
    std::uint64_t triggerDeduplicated = 0;
    std::uint64_t weaponBreaks = 0;
    std::uint8_t lastLeftTriggerType = 0;
    std::uint8_t lastRightTriggerType = 0;
    std::uint64_t rumbleUpdates = 0;
    std::uint64_t rumbleActiveUpdates = 0;
    std::uint64_t audioEnvelopeReports = 0;
    std::uint64_t audioEnvelopeActive = 0;
    std::uint64_t waveformLeftActiveBlocks = 0;
    std::uint64_t waveformRightActiveBlocks = 0;
    std::uint64_t waveformLeftPeak = 0;
    std::uint64_t waveformRightPeak = 0;
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
    std::uint64_t waveformStaleDrops = 0;
    std::uint64_t waveformMaximumAgeUs = 0;
    std::uint64_t waveformTotalAgeUs = 0;
    std::uint64_t waveformActiveRendered = 0;
    std::uint64_t gripEnvelopeFrames = 0;
    std::uint64_t gripRumbleFrames = 0;
    std::uint64_t deadlineOverruns = 0;
    std::uint64_t writeFailures = 0;
    std::uint64_t totalWriteDurationUs = 0;
    std::uint64_t maximumWriteDurationUs = 0;
    std::uint64_t leftTriggerFrames = 0;
    std::uint64_t rightTriggerFrames = 0;
    std::uint64_t bothTriggerFrames = 0;
    std::uint64_t hapticEnables = 0;
    std::uint64_t hapticDisables = 0;
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
        Clock::time_point receivedAt{};
    };

    static constexpr std::size_t kWaveformQueueCapacity = 3;

    void run() noexcept;
    flydigi::apex6::MotorBlock renderBlock(
        flydigi::apex6::TriggerRoute& route,
        bool& triggerEnabled,
        Clock::time_point now) noexcept;
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
    Apex6TriggerEffect leftTrigger_{};
    Apex6TriggerEffect rightTrigger_{};
    Apex6TriggerRenderState leftTriggerState_{};
    Apex6TriggerRenderState rightTriggerState_{};
    std::uint8_t leftPosition_ = 0;
    std::uint8_t rightPosition_ = 0;
    bool routeRightNext_ = false;
    double leftGripPhase_ = 0.0;
    double rightGripPhase_ = 0.0;

    mutable std::mutex errorMutex_;
    std::string error_;
    std::atomic_bool failed_{false};
    std::atomic_uint64_t hidReports_{0};
    std::atomic_uint64_t triggerLeftUpdates_{0};
    std::atomic_uint64_t triggerRightUpdates_{0};
    std::atomic_uint64_t triggerActiveUpdates_{0};
    std::atomic_uint64_t triggerStops_{0};
    std::atomic_uint64_t triggerUnsupported_{0};
    std::atomic_uint64_t triggerDeduplicated_{0};
    std::atomic_uint64_t weaponBreaks_{0};
    std::atomic_uint8_t lastLeftTriggerType_{0};
    std::atomic_uint8_t lastRightTriggerType_{0};
    std::atomic_uint64_t rumbleUpdates_{0};
    std::atomic_uint64_t rumbleActiveUpdates_{0};
    std::atomic_uint64_t audioEnvelopeReports_{0};
    std::atomic_uint64_t audioEnvelopeActive_{0};
    std::atomic_uint64_t waveformLeftActiveBlocks_{0};
    std::atomic_uint64_t waveformRightActiveBlocks_{0};
    std::atomic_uint64_t waveformLeftPeak_{0};
    std::atomic_uint64_t waveformRightPeak_{0};
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
    std::atomic_uint64_t waveformStaleDrops_{0};
    std::atomic_uint64_t waveformMaximumAgeUs_{0};
    std::atomic_uint64_t waveformTotalAgeUs_{0};
    std::atomic_uint64_t waveformActiveRendered_{0};
    std::atomic_uint64_t gripEnvelopeFrames_{0};
    std::atomic_uint64_t gripRumbleFrames_{0};
    std::atomic_uint64_t deadlineOverruns_{0};
    std::atomic_uint64_t writeFailures_{0};
    std::atomic_uint64_t totalWriteDurationUs_{0};
    std::atomic_uint64_t maximumWriteDurationUs_{0};
    std::atomic_uint64_t leftTriggerFrames_{0};
    std::atomic_uint64_t rightTriggerFrames_{0};
    std::atomic_uint64_t bothTriggerFrames_{0};
    std::atomic_uint64_t hapticEnables_{0};
    std::atomic_uint64_t hapticDisables_{0};
};

} // namespace asb::dualsense
