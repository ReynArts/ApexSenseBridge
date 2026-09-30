#include "dualsense/Apex6HapticBridge.h"
#include "dualsense/EffectStrength.h"

#include <algorithm>
#include <cmath>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace asb::dualsense {
namespace {

constexpr auto kFramePeriod = std::chrono::milliseconds(8);
constexpr auto kEnableDelay = std::chrono::milliseconds(94);
constexpr auto kAudioEnvelopeTimeout = std::chrono::milliseconds(100);
constexpr auto kMaximumWaveformAge = std::chrono::milliseconds(24);
constexpr double kPi = 3.14159265358979323846;

std::int8_t scalePcm(std::int16_t sample) noexcept {
    // Hardware validation confirmed that the conservative pre-validation cap
    // left substantial actuator range unused. Map PCM onto the complete safe
    // symmetric range accepted by the unsigned-bipolar 0x57 channel.
    const auto denominator = sample >= 0 ? 32767 : 32768;
    const auto scaled = static_cast<std::int32_t>(sample) * 127 / denominator;
    return static_cast<std::int8_t>(
        std::clamp<std::int32_t>(scaled, -127, 127));
}

#ifdef _WIN32
class HighResolutionDeadlineTimer {
public:
    HighResolutionDeadlineTimer() noexcept {
        constexpr DWORD kHighResolution = 0x00000002;
        timer_ = CreateWaitableTimerExW(
            nullptr, nullptr, kHighResolution, TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!timer_) {
            timer_ = CreateWaitableTimerExW(
                nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
        }
    }

    ~HighResolutionDeadlineTimer() {
        if (timer_) CloseHandle(timer_);
    }

    bool waitUntil(std::chrono::steady_clock::time_point deadline) noexcept {
        if (!timer_) return false;
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) return true;
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
            remaining).count();
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>((nanoseconds + 99) / 100);
        if (due.QuadPart == 0) due.QuadPart = -1;
        if (!SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
            return false;
        }
        return WaitForSingleObject(timer_, INFINITE) == WAIT_OBJECT_0;
    }

private:
    HANDLE timer_ = nullptr;
};
#endif

std::int8_t sineSample(double& phase, double frequency,
                       std::uint8_t amplitude) noexcept {
    const auto strength = static_cast<double>(amplitude) / 255.0 * 112.0;
    const auto value = static_cast<std::int32_t>(
        std::lround(std::sin(phase * 2.0 * kPi) * strength));
    phase += frequency / 1000.0;
    phase -= std::floor(phase);
    return static_cast<std::int8_t>(std::clamp<std::int32_t>(value, -112, 112));
}

template <typename Sample>
bool anyNonZero(const std::array<Sample, 8>& samples) noexcept {
    return std::any_of(samples.begin(), samples.end(),
                       [](Sample sample) { return sample != 0; });
}

bool sequenceIsAfter(std::uint32_t candidate, std::uint32_t previous) noexcept {
    const auto delta = candidate - previous;
    return delta != 0 && delta < 0x80000000U;
}

void updateMaximum(std::atomic_uint64_t& target, std::uint64_t value) noexcept {
    auto current = target.load(std::memory_order_relaxed);
    while (value > current &&
           !target.compare_exchange_weak(
               current, value, std::memory_order_relaxed)) {
    }
}

} // namespace

Apex6HapticBridge::Apex6HapticBridge(flydigi::Apex5Device& device,
                                     haptics::HapticConfig config,
                                     bool routeGrips, unsigned triggerStrengthPercent,
                                     unsigned vibrationStrengthPercent)
    : device_(device), hapticProcessor_(config), routeGrips_(routeGrips),
      triggerStrengthPercent_(triggerStrengthPercent), vibrationStrengthPercent_(vibrationStrengthPercent),
      activationThreshold_(config.activationThreshold) {}

Apex6HapticBridge::~Apex6HapticBridge() {
    std::string ignored;
    (void)stop(ignored);
}

bool Apex6HapticBridge::start(std::string& error) {
    {
        std::lock_guard lock(stateMutex_);
        if (started_) return true;
        stopping_ = false;
    }
    if (!device_.enableApex6Haptics(error)) return false;
    hapticEnables_.fetch_add(1, std::memory_order_relaxed);
    std::this_thread::sleep_for(kEnableDelay);
    {
        std::lock_guard lock(stateMutex_);
        started_ = true;
    }
    worker_ = std::thread([this] { run(); });
    return true;
}

bool Apex6HapticBridge::stop(std::string& error) noexcept {
    {
        std::lock_guard lock(stateMutex_);
        if (!started_ && !worker_.joinable()) return true;
        stopping_ = true;
    }
    stopSignal_.notify_all();
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard lock(stateMutex_);
        started_ = false;
        waveformQueue_.clear();
        lastWaveformSequence_.reset();
        lastWaveformAt_ = {};
        audioEnvelope_ = {};
        lastAudioEnvelopeAt_ = {};
        rumbleLow_ = 0;
        rumbleHigh_ = 0;
        diagnosticGripCarrier_ = false;
        leftTrigger_ = {};
        rightTrigger_ = {};
        leftTriggerState_.reset();
        rightTriggerState_.reset();
        leftPosition_ = 0;
        rightPosition_ = 0;
        leftGripPhase_ = 0.0;
        rightGripPhase_ = 0.0;
        lastLeftRequest_.reset();
        lastRightRequest_.reset();
    }
    std::string disableError;
    const bool disabled = device_.disableApex6Haptics(disableError);
    if (disabled) hapticDisables_.fetch_add(1, std::memory_order_relaxed);
    if (!disabled) {
        error = std::move(disableError);
        recordError(error);
    }
    return disabled;
}

void Apex6HapticBridge::handle(const DualSenseFeedback& feedback) {
    if (failed_.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(stateMutex_);
    if (feedback.kind == FeedbackKind::AudioHapticWaveform) {
        if (!routeGrips_) return;
        waveformBlocks_.fetch_add(1, std::memory_order_relaxed);
        if (feedback.hasRawAudioMeasurements && feedback.rawAudioFrames != 0) {
            rawAudioMeasuredBlocks_.fetch_add(1, std::memory_order_relaxed);
            rawAudioFrames_.fetch_add(feedback.rawAudioFrames, std::memory_order_relaxed);
            for (std::size_t channel = 0; channel < rawAudioPeaks_.size(); ++channel) {
                updateMaximum(rawAudioPeaks_[channel], feedback.rawAudioPeaks[channel]);
            }
            rawHapticLeftSumSquares_.fetch_add(
                feedback.rawHapticLeftSumSquares, std::memory_order_relaxed);
            rawHapticRightSumSquares_.fetch_add(
                feedback.rawHapticRightSumSquares, std::memory_order_relaxed);
        }
        WaveformBlock block{feedback.audioSequence,
                            feedback.leftHapticSamples,
                            feedback.rightHapticSamples,
                            Clock::now()};
        auto leftPeak = 0U;
        auto rightPeak = 0U;
        std::uint64_t leftSumSquares = 0;
        std::uint64_t rightSumSquares = 0;
        for (std::size_t index = 0; index < block.left.size(); ++index) {
            const auto leftSample = static_cast<std::int64_t>(block.left[index]);
            const auto rightSample = static_cast<std::int64_t>(block.right[index]);
            leftSumSquares += static_cast<std::uint64_t>(leftSample * leftSample);
            rightSumSquares += static_cast<std::uint64_t>(rightSample * rightSample);
            leftPeak = (std::max)(leftPeak, static_cast<unsigned>(
                std::abs(static_cast<int>(block.left[index]))));
            rightPeak = (std::max)(rightPeak, static_cast<unsigned>(
                std::abs(static_cast<int>(block.right[index]))));
        }
        if (leftPeak > 0) waveformLeftActiveBlocks_.fetch_add(1, std::memory_order_relaxed);
        if (rightPeak > 0) waveformRightActiveBlocks_.fetch_add(1, std::memory_order_relaxed);
        updateMaximum(waveformLeftPeak_, leftPeak);
        updateMaximum(waveformRightPeak_, rightPeak);
        waveformLeftSumSquares_.fetch_add(leftSumSquares, std::memory_order_relaxed);
        waveformRightSumSquares_.fetch_add(rightSumSquares, std::memory_order_relaxed);
        if (leftPeak == 0 && rightPeak == 0) {
            waveformSilentBlocks_.fetch_add(1, std::memory_order_relaxed);
        }
        // Gate each channel before scaling; never amplify the original PCM.
        if (leftPeak / 32768.0 < activationThreshold_) {
            block.left.fill(0);
            if (leftPeak != 0) waveformLeftThresholded_.fetch_add(1, std::memory_order_relaxed);
        }
        if (rightPeak / 32768.0 < activationThreshold_) {
            block.right.fill(0);
            if (rightPeak != 0) waveformRightThresholded_.fetch_add(1, std::memory_order_relaxed);
        }
        if (lastWaveformSequence_) {
            const auto delta = block.sequence - *lastWaveformSequence_;
            if (delta == 0) {
                waveformDuplicates_.fetch_add(1, std::memory_order_relaxed);
                waveformBlocksDropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (!sequenceIsAfter(block.sequence, *lastWaveformSequence_)) {
                waveformOutOfOrder_.fetch_add(1, std::memory_order_relaxed);
                waveformBlocksDropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (delta > 1) {
                waveformSequenceGaps_.fetch_add(
                    static_cast<std::uint64_t>(delta - 1),
                    std::memory_order_relaxed);
            }
        }
        lastWaveformSequence_ = block.sequence;
        lastWaveformAt_ = Clock::now();
        if (waveformQueue_.size() == kWaveformQueueCapacity) {
            const auto& dropped = waveformQueue_.front();
            if (anyNonZero(dropped.left) || anyNonZero(dropped.right)) {
                waveformActiveDrops_.fetch_add(1, std::memory_order_relaxed);
            }
            waveformQueue_.pop_front();
            waveformOverflowDrops_.fetch_add(1, std::memory_order_relaxed);
            waveformBlocksDropped_.fetch_add(1, std::memory_order_relaxed);
        }
        waveformQueue_.push_back(std::move(block));
        updateMaximum(waveformQueueMaxDepth_, waveformQueue_.size());
        return;
    }
    if (feedback.kind == FeedbackKind::AudioHaptics) {
        if (!routeGrips_) return;
        audioEnvelopeReports_.fetch_add(1, std::memory_order_relaxed);
        audioEnvelope_ = hapticProcessor_.process(feedback);
        if (audioEnvelope_.lowFrequency != 0 || audioEnvelope_.highFrequency != 0) {
            audioEnvelopeActive_.fetch_add(1, std::memory_order_relaxed);
        }
        lastAudioEnvelopeAt_ = Clock::now();
        return;
    }

    hidReports_.fetch_add(1, std::memory_order_relaxed);
    constexpr std::uint8_t kCompatibleVibration = 0x01;
    constexpr std::uint8_t kCompatibleVibration2 = 0x04;
    constexpr std::uint8_t kRightTrigger = 0x04;
    constexpr std::uint8_t kLeftTrigger = 0x08;
    if ((feedback.enableBits1 & kCompatibleVibration) != 0 ||
        (feedback.enableBits3 & kCompatibleVibration2) != 0) {
        rumbleUpdates_.fetch_add(1, std::memory_order_relaxed);
        if (feedback.rumbleLeft != 0 || feedback.rumbleRight != 0) {
            rumbleActiveUpdates_.fetch_add(1, std::memory_order_relaxed);
        }
        rumbleLow_ = routeGrips_ ? feedback.rumbleLeft : 0;
        rumbleHigh_ = routeGrips_ ? feedback.rumbleRight : 0;
        diagnosticGripCarrier_ = false;
    }
    if ((feedback.enableBits1 & kLeftTrigger) != 0) {
        handleTrigger(TriggerSide::Left, feedback);
    }
    if ((feedback.enableBits1 & kRightTrigger) != 0) {
        handleTrigger(TriggerSide::Right, feedback);
    }
}

void Apex6HapticBridge::handleTrigger(TriggerSide side, const DualSenseFeedback& feedback) {
    const bool left = side == TriggerSide::Left;
    const auto& bytes = left ? feedback.leftTriggerEffect : feedback.rightTriggerEffect;
    (left ? triggerLeftUpdates_ : triggerRightUpdates_).fetch_add(1, std::memory_order_relaxed);
    (left ? lastLeftTriggerType_ : lastRightTriggerType_).store(bytes[0], std::memory_order_relaxed);
    Apex6TriggerDecodeError decodeError{};
    const auto decoded = decodeApex6TriggerEffect(side, bytes, rumbleLow_, &decodeError);
    const auto next = decoded.value_or(Apex6TriggerEffect{});
    auto status = next.type == Apex6TriggerType::Off
        ? Apex6TriggerTraceStatus::Off : Apex6TriggerTraceStatus::Active;
    if (!decoded) {
        triggerUnsupported_.fetch_add(1, std::memory_order_relaxed);
        status = decodeError == Apex6TriggerDecodeError::InvalidParameters
            ? Apex6TriggerTraceStatus::Malformed : Apex6TriggerTraceStatus::Unsupported;
        if (status == Apex6TriggerTraceStatus::Malformed) {
            triggerMalformed_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    Apex6TriggerTraceEntry entry{};
    entry.elapsedMilliseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - traceStartedAt_).count());
    entry.side = side;
    entry.status = status;
    entry.position = left ? leftPosition_ : rightPosition_;
    entry.enableBits = feedback.enableBits1;
    entry.bytes = bytes;
    if (!decoded) (left ? lastRejectedLeft_ : lastRejectedRight_) = entry;
    else if (next.type != Apex6TriggerType::Off) (left ? lastActiveLeft_ : lastActiveRight_) = entry;
    auto& lastRequest = left ? lastLeftRequest_ : lastRightRequest_;
    if (!lastRequest || lastRequest->bytes != bytes || lastRequest->status != status) {
        triggerTrace_[triggerTraceNext_] = entry;
        triggerTraceNext_ = (triggerTraceNext_ + 1) % triggerTrace_.size();
        if (triggerTraceCount_ < triggerTrace_.size()) ++triggerTraceCount_;
        else ++triggerTraceOverwritten_;
    }
    lastRequest = entry;
    auto& target = left ? leftTrigger_ : rightTrigger_;
    if (next == target) {
        triggerDeduplicated_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    target = next;
    (left ? leftTriggerState_ : rightTriggerState_).reset();
    if (!decoded) triggerRejectedStops_.fetch_add(1, std::memory_order_relaxed);
    else (next.type == Apex6TriggerType::Off ? triggerStops_ : triggerActiveUpdates_)
        .fetch_add(1, std::memory_order_relaxed);
}

void Apex6HapticBridge::updateTriggerPositions(
    std::uint8_t left, std::uint8_t right) noexcept {
    std::lock_guard lock(stateMutex_);
    leftPosition_ = left;
    rightPosition_ = right;
}

void Apex6HapticBridge::setDiagnosticTrigger(
    const ForceTriggerCommand& command) noexcept {
    std::lock_guard lock(stateMutex_);
    Apex6TriggerEffect effect{};
    if (command.mode != TriggerMode::Normal) {
        effect.type = Apex6TriggerType::Legacy;
        effect.legacy = command;
    }
    auto& target = command.side == TriggerSide::Left ? leftTrigger_ : rightTrigger_;
    auto& state = command.side == TriggerSide::Left
        ? leftTriggerState_ : rightTriggerState_;
    if (target != effect) {
        target = effect;
        state.reset();
    }
}

void Apex6HapticBridge::setDiagnosticGrips(
    std::uint8_t left, std::uint8_t right) noexcept {
    std::lock_guard lock(stateMutex_);
    rumbleLow_ = left;
    rumbleHigh_ = right;
    diagnosticGripCarrier_ = true;
}

flydigi::apex6::MotorBlock Apex6HapticBridge::renderBlock(
    flydigi::apex6::TriggerRoute& route,
    bool& triggerEnabled,
    Clock::time_point now) noexcept {
    flydigi::apex6::MotorBlock output{};
    std::array<std::int8_t, 8> leftTrigger;
    std::array<std::int8_t, 8> rightTrigger;
    std::optional<WaveformBlock> waveform;
    haptics::MotorLevels envelope{};
    std::uint8_t rumbleLow = 0;
    std::uint8_t rumbleHigh = 0;
    bool diagnosticGripCarrier = false;
    bool waveformRecentlyReceived = false;
    {
        std::lock_guard lock(stateMutex_);
        while (!waveformQueue_.empty() &&
               now - waveformQueue_.front().receivedAt > kMaximumWaveformAge) {
            const auto& dropped = waveformQueue_.front();
            if (anyNonZero(dropped.left) || anyNonZero(dropped.right)) {
                waveformActiveDrops_.fetch_add(1, std::memory_order_relaxed);
            }
            waveformQueue_.pop_front();
            waveformStaleDrops_.fetch_add(1, std::memory_order_relaxed);
            waveformBlocksDropped_.fetch_add(1, std::memory_order_relaxed);
        }
        if (!waveformQueue_.empty()) {
            waveform = std::move(waveformQueue_.front());
            waveformQueue_.pop_front();
            const auto ageUs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    now - waveform->receivedAt).count());
            waveformTotalAgeUs_.fetch_add(ageUs, std::memory_order_relaxed);
            updateMaximum(waveformMaximumAgeUs_, ageUs);
            waveformBlocksRendered_.fetch_add(1, std::memory_order_relaxed);
        }
        waveformRecentlyReceived =
            lastWaveformAt_.time_since_epoch().count() != 0 &&
            now - lastWaveformAt_ < kAudioEnvelopeTimeout;
        if (!waveform && waveformRecentlyReceived) {
            waveformUnderruns_.fetch_add(1, std::memory_order_relaxed);
        }
        if (lastAudioEnvelopeAt_.time_since_epoch().count() != 0 &&
            now - lastAudioEnvelopeAt_ < kAudioEnvelopeTimeout) {
            envelope = audioEnvelope_;
        }
        rumbleLow = rumbleLow_;
        rumbleHigh = rumbleHigh_;
        diagnosticGripCarrier = diagnosticGripCarrier_;
        const auto leftPulseBefore = leftTriggerState_.pulseSamplesRemaining;
        const auto rightPulseBefore = rightTriggerState_.pulseSamplesRemaining;
        leftTrigger = renderApex6TriggerEffect(
            leftTrigger_, leftPosition_, leftTriggerState_);
        rightTrigger = renderApex6TriggerEffect(
            rightTrigger_, rightPosition_, rightTriggerState_);
        if (leftTriggerState_.pulseSamplesRemaining > leftPulseBefore) {
            weaponBreaks_.fetch_add(1, std::memory_order_relaxed);
        }
        if (rightTriggerState_.pulseSamplesRemaining > rightPulseBefore) {
            weaponBreaks_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    const auto leftPcmActive = waveform &&
        std::any_of(waveform->left.begin(), waveform->left.end(),
                    [](std::int16_t value) { return value != 0; });
    const auto rightPcmActive = waveform &&
        std::any_of(waveform->right.begin(), waveform->right.end(),
                    [](std::int16_t value) { return value != 0; });
    const bool leftEnvelope = !leftPcmActive && !waveformRecentlyReceived &&
                              envelope.lowFrequency != 0;
    const bool rightEnvelope = !rightPcmActive && !waveformRecentlyReceived &&
                               envelope.highFrequency != 0;
    const bool leftRumble = !leftPcmActive && !leftEnvelope && rumbleLow != 0;
    const bool rightRumble = !rightPcmActive && !rightEnvelope && rumbleHigh != 0;
    if (routeGrips_ && (leftEnvelope || rightEnvelope)) {
        gripEnvelopeFrames_.fetch_add(1, std::memory_order_relaxed);
    }
    if (routeGrips_ && (leftRumble || rightRumble)) {
        gripRumbleFrames_.fetch_add(1, std::memory_order_relaxed);
    }
    for (std::size_t index = 0; routeGrips_ && index < output.size(); ++index) {
        if (leftPcmActive) {
            output[index].leftGrip = scalePcm(waveform->left[index]);
        } else if (leftEnvelope) {
            output[index].leftGrip = sineSample(
                leftGripPhase_, 85.0, envelope.lowFrequency);
        } else if (leftRumble) {
            output[index].leftGrip = sineSample(
                leftGripPhase_, diagnosticGripCarrier ? 120.0 : 65.0,
                rumbleLow);
        }
        if (rightPcmActive) {
            output[index].rightGrip = scalePcm(waveform->right[index]);
        } else if (rightEnvelope) {
            output[index].rightGrip = sineSample(
                rightGripPhase_, 85.0, envelope.highFrequency);
        } else if (rightRumble) {
            output[index].rightGrip = sineSample(
                rightGripPhase_, diagnosticGripCarrier ? 120.0 : 150.0,
                rumbleHigh);
        }
    }

    for (std::size_t index = 0; index < output.size(); ++index) {
        output[index].leftGrip = scaleEffectStrength(output[index].leftGrip, vibrationStrengthPercent_);
        output[index].rightGrip = scaleEffectStrength(output[index].rightGrip, vibrationStrengthPercent_);
        leftTrigger[index] = scaleEffectStrength(leftTrigger[index], triggerStrengthPercent_);
        rightTrigger[index] = scaleEffectStrength(rightTrigger[index], triggerStrengthPercent_);
    }
    if ((leftPcmActive || rightPcmActive) &&
        std::any_of(output.begin(), output.end(), [leftPcmActive, rightPcmActive](const auto& sample) {
            return (leftPcmActive && sample.leftGrip != 0) ||
                   (rightPcmActive && sample.rightGrip != 0);
        })) {
        waveformActiveRendered_.fetch_add(1, std::memory_order_relaxed);
    }
    const bool leftActive = anyNonZero(leftTrigger);
    const bool rightActive = anyNonZero(rightTrigger);
    triggerEnabled = leftActive || rightActive;
    if (!triggerEnabled) {
        route = flydigi::apex6::TriggerRoute::Mute;
        return output;
    }
    if (leftActive && rightActive && leftTrigger == rightTrigger) {
        route = flydigi::apex6::TriggerRoute::Both;
        for (std::size_t index = 0; index < output.size(); ++index) {
            output[index].trigger = leftTrigger[index];
        }
        bothTriggerFrames_.fetch_add(1, std::memory_order_relaxed);
        return output;
    }

    const bool chooseRight = rightActive && (!leftActive || routeRightNext_);
    routeRightNext_ = !routeRightNext_;
    route = chooseRight ? flydigi::apex6::TriggerRoute::Right
                        : flydigi::apex6::TriggerRoute::Left;
    const auto& selected = chooseRight ? rightTrigger : leftTrigger;
    for (std::size_t index = 0; index < output.size(); ++index) {
        output[index].trigger = selected[index];
    }
    (chooseRight ? rightTriggerFrames_ : leftTriggerFrames_)
        .fetch_add(1, std::memory_order_relaxed);
    return output;
}

void Apex6HapticBridge::run() noexcept {
    auto deadline = Clock::now();
#ifdef _WIN32
    HighResolutionDeadlineTimer deadlineTimer;
#endif
    for (;;) {
        deadline += kFramePeriod;
        flydigi::apex6::TriggerRoute route{};
        bool triggerEnabled = false;
        const auto block = renderBlock(route, triggerEnabled, Clock::now());
        std::string writeError;
        const auto writeStartedAt = Clock::now();
        if (!device_.writeApex6Haptics(
                block, route, triggerEnabled, routeGrips_, writeError)) {
            writeFailures_.fetch_add(1, std::memory_order_relaxed);
            recordError(std::move(writeError));
            return;
        }
        const auto writeDurationUs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - writeStartedAt).count());
        totalWriteDurationUs_.fetch_add(writeDurationUs, std::memory_order_relaxed);
        updateMaximum(maximumWriteDurationUs_, writeDurationUs);
        framesWritten_.fetch_add(1, std::memory_order_relaxed);

        bool usedHighResolutionTimer = false;
#ifdef _WIN32
        usedHighResolutionTimer = deadlineTimer.waitUntil(deadline);
#endif
        std::unique_lock lock(stateMutex_);
        if (usedHighResolutionTimer) {
            if (stopping_) return;
        } else if (stopSignal_.wait_until(
                       lock, deadline, [this] { return stopping_; })) {
            return;
        }
        const auto now = Clock::now();
        if (now > deadline + kFramePeriod) {
            deadlineOverruns_.fetch_add(1, std::memory_order_relaxed);
            deadline = now;
        }
    }
}

void Apex6HapticBridge::recordError(std::string error) noexcept {
    {
        std::lock_guard lock(errorMutex_);
        error_ = std::move(error);
    }
    failed_.store(true, std::memory_order_release);
}

bool Apex6HapticBridge::failed() const noexcept {
    return failed_.load(std::memory_order_acquire);
}

std::string Apex6HapticBridge::error() const {
    std::lock_guard lock(errorMutex_);
    return error_;
}

Apex6HapticBridgeStats Apex6HapticBridge::stats() const noexcept {
    Apex6HapticBridgeStats result{};
    result.hidReports = hidReports_.load(std::memory_order_relaxed);
    result.triggerLeftUpdates = triggerLeftUpdates_.load(std::memory_order_relaxed);
    result.triggerRightUpdates = triggerRightUpdates_.load(std::memory_order_relaxed);
    result.triggerActiveUpdates = triggerActiveUpdates_.load(std::memory_order_relaxed);
    result.triggerStops = triggerStops_.load(std::memory_order_relaxed);
    result.triggerUnsupported = triggerUnsupported_.load(std::memory_order_relaxed);
    result.triggerMalformed = triggerMalformed_.load(std::memory_order_relaxed);
    result.triggerRejectedStops = triggerRejectedStops_.load(std::memory_order_relaxed);
    result.triggerDeduplicated = triggerDeduplicated_.load(std::memory_order_relaxed);
    result.weaponBreaks = weaponBreaks_.load(std::memory_order_relaxed);
    result.lastLeftTriggerType = lastLeftTriggerType_.load(std::memory_order_relaxed);
    result.lastRightTriggerType = lastRightTriggerType_.load(std::memory_order_relaxed);
    result.rumbleUpdates = rumbleUpdates_.load(std::memory_order_relaxed);
    result.rumbleActiveUpdates = rumbleActiveUpdates_.load(std::memory_order_relaxed);
    result.audioEnvelopeReports = audioEnvelopeReports_.load(std::memory_order_relaxed);
    result.audioEnvelopeActive = audioEnvelopeActive_.load(std::memory_order_relaxed);
    result.waveformLeftActiveBlocks = waveformLeftActiveBlocks_.load(std::memory_order_relaxed);
    result.waveformRightActiveBlocks = waveformRightActiveBlocks_.load(std::memory_order_relaxed);
    result.waveformLeftPeak = waveformLeftPeak_.load(std::memory_order_relaxed);
    result.waveformRightPeak = waveformRightPeak_.load(std::memory_order_relaxed);
    result.waveformSilentBlocks = waveformSilentBlocks_.load(std::memory_order_relaxed);
    result.waveformLeftThresholded = waveformLeftThresholded_.load(std::memory_order_relaxed);
    result.waveformRightThresholded = waveformRightThresholded_.load(std::memory_order_relaxed);
    result.waveformActiveDrops = waveformActiveDrops_.load(std::memory_order_relaxed);
    result.rawAudioMeasuredBlocks = rawAudioMeasuredBlocks_.load(std::memory_order_relaxed);
    result.rawAudioFrames = rawAudioFrames_.load(std::memory_order_relaxed);
    for (std::size_t channel = 0; channel < rawAudioPeaks_.size(); ++channel) {
        result.rawAudioPeaks[channel] = rawAudioPeaks_[channel].load(std::memory_order_relaxed);
    }
    if (result.rawAudioFrames != 0) {
        result.rawHapticLeftRms = std::sqrt(static_cast<double>(
            rawHapticLeftSumSquares_.load(std::memory_order_relaxed)) /
            static_cast<double>(result.rawAudioFrames));
        result.rawHapticRightRms = std::sqrt(static_cast<double>(
            rawHapticRightSumSquares_.load(std::memory_order_relaxed)) /
            static_cast<double>(result.rawAudioFrames));
    }
    if (result.waveformLeftActiveBlocks != 0) {
        result.waveformLeftActiveRms = std::sqrt(static_cast<double>(
            waveformLeftSumSquares_.load(std::memory_order_relaxed)) /
            (static_cast<double>(result.waveformLeftActiveBlocks) * 8.0));
    }
    if (result.waveformRightActiveBlocks != 0) {
        result.waveformRightActiveRms = std::sqrt(static_cast<double>(
            waveformRightSumSquares_.load(std::memory_order_relaxed)) /
            (static_cast<double>(result.waveformRightActiveBlocks) * 8.0));
    }
    result.framesWritten = framesWritten_.load(std::memory_order_relaxed);
    result.waveformBlocks = waveformBlocks_.load(std::memory_order_relaxed);
    result.waveformBlocksRendered =
        waveformBlocksRendered_.load(std::memory_order_relaxed);
    result.waveformBlocksDropped =
        waveformBlocksDropped_.load(std::memory_order_relaxed);
    result.waveformQueueMaxDepth =
        waveformQueueMaxDepth_.load(std::memory_order_relaxed);
    result.waveformSequenceGaps =
        waveformSequenceGaps_.load(std::memory_order_relaxed);
    result.waveformDuplicates =
        waveformDuplicates_.load(std::memory_order_relaxed);
    result.waveformOutOfOrder =
        waveformOutOfOrder_.load(std::memory_order_relaxed);
    result.waveformUnderruns =
        waveformUnderruns_.load(std::memory_order_relaxed);
    result.waveformOverflowDrops =
        waveformOverflowDrops_.load(std::memory_order_relaxed);
    result.waveformStaleDrops = waveformStaleDrops_.load(std::memory_order_relaxed);
    result.waveformMaximumAgeUs = waveformMaximumAgeUs_.load(std::memory_order_relaxed);
    result.waveformTotalAgeUs = waveformTotalAgeUs_.load(std::memory_order_relaxed);
    result.waveformActiveRendered = waveformActiveRendered_.load(std::memory_order_relaxed);
    result.gripEnvelopeFrames = gripEnvelopeFrames_.load(std::memory_order_relaxed);
    result.gripRumbleFrames = gripRumbleFrames_.load(std::memory_order_relaxed);
    result.deadlineOverruns = deadlineOverruns_.load(std::memory_order_relaxed);
    result.writeFailures = writeFailures_.load(std::memory_order_relaxed);
    result.totalWriteDurationUs =
        totalWriteDurationUs_.load(std::memory_order_relaxed);
    result.maximumWriteDurationUs =
        maximumWriteDurationUs_.load(std::memory_order_relaxed);
    result.leftTriggerFrames =
        leftTriggerFrames_.load(std::memory_order_relaxed);
    result.rightTriggerFrames =
        rightTriggerFrames_.load(std::memory_order_relaxed);
    result.bothTriggerFrames =
        bothTriggerFrames_.load(std::memory_order_relaxed);
    result.hapticEnables = hapticEnables_.load(std::memory_order_relaxed);
    result.hapticDisables = hapticDisables_.load(std::memory_order_relaxed);
    {
        std::lock_guard lock(stateMutex_);
        result.lastActiveLeft = lastActiveLeft_;
        result.lastActiveRight = lastActiveRight_;
        result.lastRejectedLeft = lastRejectedLeft_;
        result.lastRejectedRight = lastRejectedRight_;
        result.triggerTraceCount = triggerTraceCount_;
        result.triggerTraceOverwritten = triggerTraceOverwritten_;
        const auto first = triggerTraceCount_ == triggerTrace_.size() ? triggerTraceNext_ : 0;
        for (std::size_t index = 0; index < triggerTraceCount_; ++index) {
            result.triggerTrace[index] = triggerTrace_[(first + index) % triggerTrace_.size()];
        }
    }
    return result;
}

} // namespace asb::dualsense
