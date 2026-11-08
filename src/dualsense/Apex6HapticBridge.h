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
#include <string_view>
#include <thread>

namespace asb::dualsense {

enum class Apex6TriggerTraceStatus : std::uint8_t {
    Active,
    Off,
    Unsupported,
    Malformed,
};

struct Apex6TriggerTraceEntry {
    std::uint64_t elapsedMilliseconds = 0;
    TriggerSide side = TriggerSide::Left;
    Apex6TriggerTraceStatus status = Apex6TriggerTraceStatus::Off;
    std::uint8_t position = 0;
    std::uint8_t enableBits = 0;
    std::array<std::uint8_t, 11> bytes{};
};

[[nodiscard]] constexpr std::string_view apex6TriggerTraceStatusName(
    Apex6TriggerTraceStatus status) noexcept {
    switch (status) {
    case Apex6TriggerTraceStatus::Active: return "active";
    case Apex6TriggerTraceStatus::Off: return "off";
    case Apex6TriggerTraceStatus::Unsupported: return "unsupported";
    case Apex6TriggerTraceStatus::Malformed: return "malformed";
    }
    return "unknown";
}

struct Apex6ReportSignature {
    std::uint8_t enableBits1 = 0;
    std::uint8_t enableBits2 = 0;
    std::uint8_t enableBits3 = 0;
    std::uint8_t leftMode = 0;
    std::uint8_t rightMode = 0;
    bool rumble = false;
    std::uint64_t count = 0;
    std::uint64_t firstMilliseconds = 0;
    std::uint64_t lastMilliseconds = 0;
};

enum class Apex6DualPolicy : std::uint8_t {
    Mix,
    Priority,
    Firmware,
};

inline constexpr Apex6DualPolicy kApex6DefaultDualPolicy = Apex6DualPolicy::Mix;

[[nodiscard]] constexpr std::string_view apex6DualPolicyName(Apex6DualPolicy policy) noexcept {
    switch (policy) {
    case Apex6DualPolicy::Mix: return "mix";
    case Apex6DualPolicy::Priority: return "priority";
    case Apex6DualPolicy::Firmware: return "firmware";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::optional<Apex6DualPolicy> apex6DualPolicyFromName(
    std::string_view name) noexcept {
    if (name == "mix") return Apex6DualPolicy::Mix;
    if (name == "priority") return Apex6DualPolicy::Priority;
    if (name == "firmware") return Apex6DualPolicy::Firmware;
    return std::nullopt;
}

inline constexpr int kApex6DualMixPeak = 127;
inline constexpr unsigned kApex6DualMixReleaseBlocks = 63;
inline constexpr unsigned kApex6DualEntryWindowBlocks = 3;
inline constexpr unsigned kApex6DualYieldSilentBlocks = 8;
inline constexpr unsigned kApex6DualOwnerHoldBlocks = 32;
inline constexpr unsigned kApex6DualPriorityReleaseBlocks = 32;
inline constexpr unsigned kApex6DualFirmwareReleaseBlocks = 250;
inline constexpr unsigned kApex6DualFirmwareOffGraceBlocks = 13;
inline constexpr unsigned kApex6DualFirmwareRefreshBlocks = 25;
inline constexpr unsigned kApex6DualMaximumSlackWrites = 2;
inline constexpr double kApex6FirmwareLoopStrengthScale = 150.0;
inline constexpr double kApex6FirmwareOneShotStrengthScale = 255.0;
inline constexpr double kApex6FirmwareTrainFullDrive = 150.0;

[[nodiscard]] std::optional<flydigi::apex6::TriggerFirmwareEffect> apex6FirmwareEffectFor(
    const Apex6TriggerEffect& effect) noexcept;

enum class Apex6DualChange : std::uint8_t {
    None,
    Episode,
    Burst,
    Return,
    NewEffect,
    Yield,
};

struct Apex6DualInput {
    std::uint64_t block = 0;
    bool leftActive = false;
    bool rightActive = false;
    bool identical = false;
    bool leftBurst = false;
    bool rightBurst = false;
    bool leftNewEffect = false;
    bool rightNewEffect = false;
    bool leftEffectOn = false;
    bool rightEffectOn = false;
    bool firmwareCapable = false;
    Apex6DualPolicy policy = kApex6DefaultDualPolicy;
};

struct Apex6DualDecision {
    bool episode = false;
    bool mix = false;
    bool firmware = false;
    flydigi::apex6::TriggerRoute route = flydigi::apex6::TriggerRoute::Mute;
    flydigi::apex6::TriggerRoute owner = flydigi::apex6::TriggerRoute::Mute;
    Apex6DualChange change = Apex6DualChange::None;
};

class Apex6DualArbiter {
public:
    [[nodiscard]] Apex6DualDecision next(const Apex6DualInput& input) noexcept;
    void reset() noexcept { *this = {}; }

private:
    std::uint64_t leftSeen_ = 0;
    std::uint64_t rightSeen_ = 0;
    std::uint64_t leftSince_ = 0;
    std::uint64_t rightSince_ = 0;
    std::uint64_t leftOn_ = 0;
    std::uint64_t rightOn_ = 0;
    std::uint64_t lastChange_ = 0;
    flydigi::apex6::TriggerRoute owner_ = flydigi::apex6::TriggerRoute::Mute;
    flydigi::apex6::TriggerRoute preempted_ = flydigi::apex6::TriggerRoute::Mute;
    bool episode_ = false;
    bool firmwareLatched_ = false;
};

struct Apex6HapticBridgeStats {
    std::uint64_t hidReports = 0;
    std::uint64_t triggerLeftUpdates = 0;
    std::uint64_t triggerRightUpdates = 0;
    std::uint64_t triggerActiveUpdates = 0;
    std::uint64_t triggerStops = 0;
    std::uint64_t triggerUnsupported = 0;
    std::uint64_t triggerMalformed = 0;
    std::uint64_t triggerRejectedStops = 0;
    std::uint64_t triggerDeduplicated = 0;
    std::uint64_t weaponBreaks = 0;
    std::uint64_t bowBreaks = 0;
    std::uint8_t lastLeftTriggerType = 0;
    std::uint8_t lastRightTriggerType = 0;
    std::uint64_t rumbleUpdates = 0;
    std::uint64_t rumbleActiveUpdates = 0;
    std::uint64_t rumbleLeftPeak = 0;
    std::uint64_t rumbleRightPeak = 0;
    std::uint64_t rumbleIgnoredReports = 0;
    std::uint8_t lastIgnoredRumbleEnableBits = 0;
    std::uint64_t audioEnvelopeReports = 0;
    std::uint64_t audioEnvelopeActive = 0;
    std::uint64_t waveformLeftActiveBlocks = 0;
    std::uint64_t waveformRightActiveBlocks = 0;
    std::uint64_t waveformLeftPeak = 0;
    std::uint64_t waveformRightPeak = 0;
    std::uint64_t waveformSilentBlocks = 0;
    std::uint64_t waveformLeftThresholded = 0;
    std::uint64_t waveformRightThresholded = 0;
    std::uint64_t waveformActiveDrops = 0;
    double waveformLeftActiveRms = 0.0;
    double waveformRightActiveRms = 0.0;
    std::uint64_t rawAudioMeasuredBlocks = 0;
    std::uint64_t rawAudioFrames = 0;
    std::array<std::uint64_t, 4> rawAudioPeaks{};
    double rawHapticLeftRms = 0.0;
    double rawHapticRightRms = 0.0;
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
    unsigned pcmGainPercent = 100;
    unsigned gripDuckPercent = 0;
    unsigned triggerCeilingPercent = 100;
    std::uint64_t gripDuckedBlocks = 0;
    std::uint64_t triggerLimitedSamples = 0;
    std::uint64_t pcmOutputLeftPeak = 0;
    std::uint64_t pcmOutputRightPeak = 0;
    std::uint64_t gripEnvelopeFrames = 0;
    std::uint64_t gripRumbleFrames = 0;
    std::uint64_t gripRumbleMaskedFrames = 0;
    std::uint64_t deadlineOverruns = 0;
    std::uint64_t idleFramesSkipped = 0;
    std::uint64_t idleKeepalives = 0;
    std::uint64_t silentWaveformSkipped = 0;
    std::uint64_t writeFailures = 0;
    std::uint64_t totalWriteDurationUs = 0;
    std::uint64_t maximumWriteDurationUs = 0;
    std::uint64_t leftTriggerFrames = 0;
    std::uint64_t rightTriggerFrames = 0;
    std::uint64_t bothTriggerFrames = 0;
    std::uint64_t triggerRetargets = 0;
    std::uint64_t renderedBlocks = 0;
    std::uint64_t renderTotalNs = 0;
    std::uint64_t renderMaximumNs = 0;
    std::uint64_t dualTriggerBlocks = 0;
    std::uint64_t dualEpisodes = 0;
    std::uint64_t dualBurstSwitches = 0;
    std::uint64_t dualOnsetSwitches = 0;
    std::uint64_t dualYieldSwitches = 0;
    std::uint64_t dualMixFrames = 0;
    std::uint64_t dualMixClippedSamples = 0;
    std::uint64_t dualReturns = 0;
    std::uint64_t dualOwnerChanges = 0;
    std::uint64_t dualFirmwareBlocks = 0;
    std::uint64_t dualFirmwareEntries = 0;
    std::uint64_t dualFirmwareExits = 0;
    std::uint64_t dualFirmwareRefreshes = 0;
    std::uint64_t dualFirmwareWrites = 0;
    std::uint64_t dualFirmwareWriteUs = 0;
    std::uint64_t dualFirmwareDeferred = 0;
    std::uint64_t dualFirmwareUnmapped = 0;
    Apex6DualPolicy dualPolicy = kApex6DefaultDualPolicy;
    std::uint64_t hapticEnables = 0;
    std::uint64_t hapticDisables = 0;
    unsigned sampleRateHz = 1000;
    std::uint64_t waveform2kBlocks = 0;
    std::uint64_t waveformUpsampledBlocks = 0;
    std::uint64_t primeReports = 0;
    std::optional<Apex6TriggerTraceEntry> lastActiveLeft;
    std::optional<Apex6TriggerTraceEntry> lastActiveRight;
    std::optional<Apex6TriggerTraceEntry> lastRejectedLeft;
    std::optional<Apex6TriggerTraceEntry> lastRejectedRight;
    std::array<Apex6TriggerTraceEntry, 64> triggerTrace{};
    std::size_t triggerTraceCount = 0;
    std::uint64_t triggerTraceOverwritten = 0;
    std::array<Apex6ReportSignature, 16> reportSignatures{};
    std::size_t reportSignatureCount = 0;
    std::uint64_t reportSignatureOverflow = 0;
};

class Apex6HapticBridge {
public:
    explicit Apex6HapticBridge(flydigi::Apex5Device& device,
                               haptics::HapticConfig config = {},
                               bool routeGrips = true, unsigned triggerStrengthPercent = 100,
                               unsigned vibrationStrengthPercent = 100,
                               unsigned pcmGainPercent = 100,
                               unsigned gripDuckPercent = 0,
                               unsigned triggerCeilingPercent = 100,
                               unsigned sampleRateHz = 1000);
    ~Apex6HapticBridge();

    Apex6HapticBridge(const Apex6HapticBridge&) = delete;
    Apex6HapticBridge& operator=(const Apex6HapticBridge&) = delete;

    bool start(std::string& error);
    bool stop(std::string& error) noexcept;
    void handle(const DualSenseFeedback& feedback);
    void updateTriggerPositions(std::uint8_t left, std::uint8_t right) noexcept;
    void setDiagnosticTrigger(const ForceTriggerCommand& command) noexcept;
    void setDiagnosticEffect(TriggerSide side, const Apex6TriggerEffect& effect) noexcept;
    void setDiagnosticGrips(std::uint8_t left, std::uint8_t right) noexcept;
    void setDualPolicy(Apex6DualPolicy policy) noexcept;
    [[nodiscard]] Apex6DualPolicy dualPolicy() const noexcept;

    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] Apex6HapticBridgeStats stats() const noexcept;

private:
    using Clock = std::chrono::steady_clock;

    struct WaveformBlock {
        std::uint32_t sequence = 0;
        std::array<std::int16_t, 16> left{};
        std::array<std::int16_t, 16> right{};
        Clock::time_point receivedAt{};
        std::size_t count = 8;
    };

    using WideBlock = std::array<flydigi::apex6::MotorSubframe, 16>;

    static constexpr std::size_t kWaveformQueueCapacity = 3;

    void run() noexcept;
    WideBlock renderBlock(
        flydigi::apex6::TriggerRoute& route,
        bool& triggerEnabled,
        Clock::time_point now) noexcept;
    void recordError(std::string error) noexcept;
    void wakeLocked() noexcept;
    bool tryEnterIdle(Clock::time_point now) noexcept;
    void handleTrigger(TriggerSide side, const DualSenseFeedback& feedback);
    void recordReportSignature(const DualSenseFeedback& feedback);
    void noteTriggerChangeLocked(TriggerSide side) noexcept;

    flydigi::Apex5Device& device_;
    haptics::HapticProcessor hapticProcessor_;
    bool routeGrips_ = true;
    flydigi::apex6::TriggerRoute renderedSide_ = flydigi::apex6::TriggerRoute::Mute;
    unsigned triggerStrengthPercent_ = 100;
    unsigned vibrationStrengthPercent_ = 100;
    unsigned pcmGainPercent_ = 100;
    unsigned gripDuckPercent_ = 0;
    unsigned triggerCeilingPercent_ = 100;
    bool rate2k_ = false;
    std::size_t blockSize_ = 8;
    std::int8_t leftTriggerPrevious_ = 0;
    std::int8_t rightTriggerPrevious_ = 0;
    std::int16_t leftPcmPrevious_ = 0;
    std::int16_t rightPcmPrevious_ = 0;
    std::uint64_t triggerSampleOffset_ = 0;
    std::array<bool, 2> pendingClear_{};
    std::array<Clock::time_point, 2> pendingClearAt_{};
    mutable std::mutex stateMutex_;
    std::condition_variable stopSignal_;
    std::thread worker_;
    bool stopping_ = false;
    bool wakeRequested_ = false;
    bool idle_ = false;
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
    Apex6DualPolicy dualPolicy_ = kApex6DefaultDualPolicy;
    Apex6DualArbiter dualArbiter_{};
    std::uint64_t dualBlock_ = 0;
    Apex6TriggerEffect leftLastEffect_{};
    Apex6TriggerEffect rightLastEffect_{};
    std::optional<flydigi::apex6::TriggerFirmwareEffect> leftFirmwareMapping_;
    std::optional<flydigi::apex6::TriggerFirmwareEffect> rightFirmwareMapping_;
    std::uint64_t leftEffectGeneration_ = 0;
    std::uint64_t rightEffectGeneration_ = 0;
    std::uint64_t leftEffectSeen_ = 0;
    std::uint64_t rightEffectSeen_ = 0;
    bool firmwareEngaged_ = false;
    bool dualFirmwareWanted_ = false;
    flydigi::apex6::TriggerRoute dualOwner_ = flydigi::apex6::TriggerRoute::Mute;
    std::optional<flydigi::apex6::TriggerFirmwareEffect> leftFirmwareTarget_;
    std::optional<flydigi::apex6::TriggerFirmwareEffect> rightFirmwareTarget_;
    double leftGripPhase_ = 0.0;
    double rightGripPhase_ = 0.0;
    Clock::time_point traceStartedAt_ = Clock::now();
    std::array<Apex6TriggerTraceEntry, 64> triggerTrace_{};
    std::size_t triggerTraceCount_ = 0;
    std::size_t triggerTraceNext_ = 0;
    std::uint64_t triggerTraceOverwritten_ = 0;
    std::array<Apex6ReportSignature, 16> reportSignatures_{};
    std::size_t reportSignatureCount_ = 0;
    std::uint64_t reportSignatureOverflow_ = 0;
    std::optional<Apex6TriggerTraceEntry> lastLeftRequest_;
    std::optional<Apex6TriggerTraceEntry> lastRightRequest_;
    std::optional<Apex6TriggerTraceEntry> lastActiveLeft_;
    std::optional<Apex6TriggerTraceEntry> lastActiveRight_;
    std::optional<Apex6TriggerTraceEntry> lastRejectedLeft_;
    std::optional<Apex6TriggerTraceEntry> lastRejectedRight_;

    mutable std::mutex errorMutex_;
    std::string error_;
    std::atomic_bool failed_{false};
    std::atomic_uint64_t hidReports_{0};
    std::atomic_uint64_t triggerLeftUpdates_{0};
    std::atomic_uint64_t triggerRightUpdates_{0};
    std::atomic_uint64_t triggerActiveUpdates_{0};
    std::atomic_uint64_t triggerStops_{0};
    std::atomic_uint64_t triggerUnsupported_{0};
    std::atomic_uint64_t triggerMalformed_{0};
    std::atomic_uint64_t triggerRejectedStops_{0};
    std::atomic_uint64_t triggerDeduplicated_{0};
    std::atomic_uint64_t weaponBreaks_{0};
    std::atomic_uint64_t bowBreaks_{0};
    std::atomic_uint8_t lastLeftTriggerType_{0};
    std::atomic_uint8_t lastRightTriggerType_{0};
    std::atomic_uint64_t rumbleUpdates_{0};
    std::atomic_uint64_t rumbleActiveUpdates_{0};
    std::atomic_uint64_t rumbleLeftPeak_{0};
    std::atomic_uint64_t rumbleRightPeak_{0};
    std::atomic_uint64_t rumbleIgnoredReports_{0};
    std::atomic_uint8_t lastIgnoredRumbleEnableBits_{0};
    std::atomic_uint64_t audioEnvelopeReports_{0};
    std::atomic_uint64_t audioEnvelopeActive_{0};
    std::atomic_uint64_t waveformLeftActiveBlocks_{0};
    std::atomic_uint64_t waveformRightActiveBlocks_{0};
    std::atomic_uint64_t waveformLeftPeak_{0};
    std::atomic_uint64_t waveformRightPeak_{0};
    std::atomic_uint64_t waveformSilentBlocks_{0};
    std::atomic_uint64_t waveformLeftThresholded_{0};
    std::atomic_uint64_t waveformRightThresholded_{0};
    std::atomic_uint64_t waveformActiveDrops_{0};
    std::atomic_uint64_t waveformLeftSumSquares_{0};
    std::atomic_uint64_t waveformRightSumSquares_{0};
    std::atomic_uint64_t rawAudioMeasuredBlocks_{0};
    std::atomic_uint64_t rawAudioFrames_{0};
    std::array<std::atomic_uint64_t, 4> rawAudioPeaks_{};
    std::atomic_uint64_t rawHapticLeftSumSquares_{0};
    std::atomic_uint64_t rawHapticRightSumSquares_{0};
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
    std::atomic_uint64_t gripDuckedBlocks_{0};
    std::atomic_uint64_t triggerLimitedSamples_{0};
    std::atomic_uint64_t pcmOutputLeftPeak_{0};
    std::atomic_uint64_t pcmOutputRightPeak_{0};
    std::atomic_uint64_t gripEnvelopeFrames_{0};
    std::atomic_uint64_t gripRumbleFrames_{0};
    std::atomic_uint64_t gripRumbleMaskedFrames_{0};
    std::atomic_uint64_t deadlineOverruns_{0};
    std::atomic_uint64_t idleFramesSkipped_{0};
    std::atomic_uint64_t idleKeepalives_{0};
    std::atomic_uint64_t silentWaveformSkipped_{0};
    std::atomic_uint64_t writeFailures_{0};
    std::atomic_uint64_t totalWriteDurationUs_{0};
    std::atomic_uint64_t maximumWriteDurationUs_{0};
    std::atomic_uint64_t leftTriggerFrames_{0};
    std::atomic_uint64_t rightTriggerFrames_{0};
    std::atomic_uint64_t bothTriggerFrames_{0};
    std::atomic_uint64_t triggerRetargets_{0};
    std::atomic_uint64_t renderedBlocks_{0};
    std::atomic_uint64_t renderTotalNs_{0};
    std::atomic_uint64_t renderMaximumNs_{0};
    std::atomic_uint64_t dualTriggerBlocks_{0};
    std::atomic_uint64_t dualEpisodes_{0};
    std::atomic_uint64_t dualBurstSwitches_{0};
    std::atomic_uint64_t dualOnsetSwitches_{0};
    std::atomic_uint64_t dualYieldSwitches_{0};
    std::atomic_uint64_t dualMixFrames_{0};
    std::atomic_uint64_t dualMixClippedSamples_{0};
    std::atomic_uint64_t dualReturns_{0};
    std::atomic_uint64_t dualOwnerChanges_{0};
    std::atomic_uint64_t dualFirmwareBlocks_{0};
    std::atomic_uint64_t dualFirmwareEntries_{0};
    std::atomic_uint64_t dualFirmwareExits_{0};
    std::atomic_uint64_t dualFirmwareRefreshes_{0};
    std::atomic_uint64_t dualFirmwareWrites_{0};
    std::atomic_uint64_t dualFirmwareWriteUs_{0};
    std::atomic_uint64_t dualFirmwareDeferred_{0};
    std::atomic_uint64_t dualFirmwareUnmapped_{0};
    std::atomic_uint64_t hapticEnables_{0};
    std::atomic_uint64_t hapticDisables_{0};
    std::atomic_uint64_t waveform2kBlocks_{0};
    std::atomic_uint64_t waveformUpsampledBlocks_{0};
    std::atomic_uint64_t primeReports_{0};
};

} // namespace asb::dualsense
