#include "dualsense/Apex6HapticBridge.h"
#include "dualsense/Apex6HapticShaping.h"
#include "dualsense/Apex6PcmGain.h"
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
constexpr auto kIdleKeepalive = std::chrono::milliseconds(50);
constexpr unsigned kIdleEnterFrames = 8;
constexpr unsigned kTriggerReleaseFrames = 4;
constexpr auto kEnableDelay = std::chrono::milliseconds(94);
constexpr auto kAudioEnvelopeTimeout = std::chrono::milliseconds(100);
constexpr auto kMaximumWaveformAge = std::chrono::milliseconds(24);
constexpr auto kHalfFramePeriod = std::chrono::milliseconds(4);
constexpr unsigned kPrimeHalfFrames2k = 4;
constexpr std::uint64_t kSlackWriteMarginUs = 200;
constexpr std::uint64_t kSlackWriteMinimumUs = 1000;
constexpr unsigned kZoneCount = 10;
constexpr unsigned kStrokeRange = 192;
constexpr unsigned kAxisRange = 255;
constexpr double kFirmwareFeedbackHz = 85.0;
constexpr double kZoneStrengthMaximum = 8.0;
constexpr double kMachineLevelMaximum = 7.0 * kApex6TriggerMachineDrivePerLevel;
constexpr double kLegacyForceFull = kApex6TriggerBurstFullForce;
constexpr double kPi = 3.14159265358979323846;

haptics::HapticConfig apex6HapticConfig(haptics::HapticConfig config) noexcept {
    // The shared user threshold protects APEX 4/5 eccentric grip motors.
    // APEX 6 voice coils retain low-level envelopes as well as native PCM.
    // Keep this policy inside the APEX 6 renderer for CLI/Tray/Playnite alike.
    config.activationThreshold = 0.0;
    return config;
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

template <typename Sample, std::size_t Count>
bool anyNonZero(const std::array<Sample, Count>& samples) noexcept {
    return std::any_of(samples.begin(), samples.end(),
                       [](Sample sample) { return sample != 0; });
}

template <typename Sample, std::size_t Count>
void expandBlock(const std::array<Sample, 8>& input, Sample& previous, std::size_t count,
                 std::array<Sample, Count>& output) noexcept {
    if (count == 8) {
        std::copy(input.begin(), input.end(), output.begin());
        previous = input.back();
        return;
    }
    for (std::size_t index = 0; index < input.size(); ++index) {
        output[index * 2] = static_cast<Sample>(
            (static_cast<int>(previous) + static_cast<int>(input[index])) >> 1);
        output[index * 2 + 1] = input[index];
        previous = input[index];
    }
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

flydigi::apex6::TriggerRoute otherSide(flydigi::apex6::TriggerRoute side) noexcept {
    return side == flydigi::apex6::TriggerRoute::Left ? flydigi::apex6::TriggerRoute::Right
                                                      : flydigi::apex6::TriggerRoute::Left;
}

bool singleSide(flydigi::apex6::TriggerRoute side) noexcept {
    return side == flydigi::apex6::TriggerRoute::Left ||
           side == flydigi::apex6::TriggerRoute::Right;
}

std::uint8_t zonePosition(std::uint8_t zone) noexcept {
    return static_cast<std::uint8_t>((static_cast<unsigned>(zone) * 256U + 9U) / 10U);
}

std::uint8_t strokePosition(unsigned stroke) noexcept {
    return static_cast<std::uint8_t>((std::min)(kAxisRange, stroke * kAxisRange / kStrokeRange));
}

std::optional<flydigi::apex6::TriggerFirmwareEffect> firmwareEffect(
    std::uint8_t type, double level, double hertz, unsigned start) noexcept {
    if (!std::isfinite(level) || level <= 0.0) return std::nullopt;
    const bool loop = type == flydigi::apex6::kTriggerFirmwareLoop;
    const double scale = loop ? kApex6FirmwareLoopStrengthScale : kApex6FirmwareOneShotStrengthScale;
    flydigi::apex6::TriggerFirmwareEffect result;
    result.type = type;
    result.strength = static_cast<std::uint8_t>(std::clamp<long>(
        std::lround((std::min)(level, 1.0) * scale), 1L, static_cast<long>(scale)));
    if (loop) {
        if (!std::isfinite(hertz) || hertz <= 0.0) return std::nullopt;
        result.parameter = static_cast<std::uint8_t>(std::clamp<long>(std::lround(hertz), 1L, 255L));
    }
    result.start = static_cast<std::uint8_t>((std::min)(start, kAxisRange));
    return result;
}

std::optional<std::uint8_t> trainParameter(const Apex6RhythmShape& rhythm) noexcept {
    constexpr std::array<std::pair<Apex6WeaponPreset, std::uint8_t>, 4> kTrains{{
        {Apex6WeaponPreset::MachineGun, std::uint8_t{4}},
        {Apex6WeaponPreset::Rifle, std::uint8_t{10}},
        {Apex6WeaponPreset::SubmachineGun, std::uint8_t{16}},
        {Apex6WeaponPreset::LightMachineGun, std::uint8_t{40}},
    }};
    for (const auto& [preset, parameter] : kTrains) {
        const auto reference = makeApex6WeaponPreset(preset, 4);
        if (reference.rhythm.pulse == rhythm.pulse &&
            reference.rhythm.periodSamples == rhythm.periodSamples) {
            return parameter;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<flydigi::apex6::TriggerFirmwareEffect> apex6FirmwareEffectFor(
    const Apex6TriggerEffect& effect) noexcept {
    using flydigi::apex6::kTriggerFirmwareLoop;
    using flydigi::apex6::kTriggerFirmwareOneShot;
    const auto zoneMaximum = static_cast<double>(
        *std::max_element(effect.zoneStrengths.begin(), effect.zoneStrengths.end()));
    switch (effect.type) {
    case Apex6TriggerType::Off:
        return std::nullopt;
    case Apex6TriggerType::Feedback:
        return firmwareEffect(kTriggerFirmwareLoop, zoneMaximum / kZoneStrengthMaximum,
                              kFirmwareFeedbackHz, zonePosition(effect.startZone));
    case Apex6TriggerType::Vibration:
        return firmwareEffect(kTriggerFirmwareLoop, zoneMaximum / kZoneStrengthMaximum,
                              effect.frequency, zonePosition(effect.startZone));
    case Apex6TriggerType::Weapon:
        return firmwareEffect(kTriggerFirmwareOneShot, effect.strength / kZoneStrengthMaximum,
                              0.0, zonePosition(effect.endZone));
    case Apex6TriggerType::Bow:
        return firmwareEffect(kTriggerFirmwareOneShot, effect.snapStrength / kZoneStrengthMaximum,
                              0.0, zonePosition(effect.endZone));
    case Apex6TriggerType::Impact:
        return firmwareEffect(kTriggerFirmwareOneShot,
                              effect.impactDrive / static_cast<double>(kApex6TriggerBurstMaximumDrive),
                              0.0, effect.triggerPosition);
    case Apex6TriggerType::Rhythm: {
        const auto& rhythm = effect.rhythm;
        if (rhythm.periodSamples == 0) return std::nullopt;
        if (rhythm.modulated) {
            return firmwareEffect(kTriggerFirmwareLoop,
                                  (std::max)(rhythm.levelA, rhythm.levelB) / kMachineLevelMaximum,
                                  rhythm.pulse.frequency, rhythm.activeFrom);
        }
        const double level = rhythm.drive / kApex6FirmwareTrainFullDrive;
        if (const auto parameter = trainParameter(rhythm)) {
            return firmwareEffect(kTriggerFirmwareLoop, level, *parameter, rhythm.activeFrom);
        }
        return firmwareEffect(kTriggerFirmwareLoop, level, 1000.0 / rhythm.periodSamples,
                              rhythm.activeFrom);
    }
    case Apex6TriggerType::Legacy: {
        const auto& command = effect.legacy;
        const unsigned start = strokePosition(command.params[0]);
        switch (command.mode) {
        case TriggerMode::Race:
            return firmwareEffect(kTriggerFirmwareLoop, command.params[1] / static_cast<double>(
                kApex6TriggerNativeMaximumDrive), kFirmwareFeedbackHz, start);
        case TriggerMode::RecoilRattle:
        case TriggerMode::Vibration:
            return firmwareEffect(kTriggerFirmwareLoop, command.params[2] / static_cast<double>(
                kApex6TriggerNativeMaximumDrive),
                command.params[3] == 0 ? kFirmwareFeedbackHz : command.params[3], start);
        case TriggerMode::SniperBreak:
            return firmwareEffect(kTriggerFirmwareOneShot, command.params[2] / kLegacyForceFull, 0.0,
                                  strokePosition(static_cast<unsigned>(command.params[0]) +
                                                 command.params[1]));
        case TriggerMode::Lock:
            return firmwareEffect(kTriggerFirmwareOneShot,
                                  kApex6TriggerNativeMaximumDrive /
                                      static_cast<double>(kApex6TriggerBurstMaximumDrive),
                                  0.0, start);
        case TriggerMode::Normal:
            return std::nullopt;
        }
        return std::nullopt;
    }
    }
    return std::nullopt;
}

Apex6DualDecision Apex6DualArbiter::next(const Apex6DualInput& input) noexcept {
    using Route = flydigi::apex6::TriggerRoute;
    const auto stamp = input.block + 1;
    const auto since = [stamp](std::uint64_t seen) {
        return seen == 0 || seen > stamp ? UINT64_MAX : stamp - seen;
    };
    if (input.leftActive) {
        if (since(leftSeen_) > kApex6DualEntryWindowBlocks) leftSince_ = stamp;
        leftSeen_ = stamp;
    }
    if (input.rightActive) {
        if (since(rightSeen_) > kApex6DualEntryWindowBlocks) rightSince_ = stamp;
        rightSeen_ = stamp;
    }
    if (input.leftEffectOn) leftOn_ = stamp;
    if (input.rightEffectOn) rightOn_ = stamp;
    const auto active = [&input](Route side) {
        return side == Route::Left ? input.leftActive : input.rightActive;
    };
    const auto burst = [&input](Route side) {
        return side == Route::Left ? input.leftBurst : input.rightBurst;
    };
    const auto fresh = [&input](Route side) {
        return side == Route::Left ? input.leftNewEffect : input.rightNewEffect;
    };
    const auto seen = [&](Route side) { return since(side == Route::Left ? leftSeen_ : rightSeen_); };
    const bool firmware = input.policy == Apex6DualPolicy::Firmware && input.firmwareCapable;
    const bool mix = input.policy == Apex6DualPolicy::Mix;
    const std::uint64_t release = firmware ? kApex6DualFirmwareReleaseBlocks
                                  : mix    ? kApex6DualMixReleaseBlocks
                                           : kApex6DualPriorityReleaseBlocks;
    Apex6DualDecision decision;
    if (episode_ && (input.identical || since(leftSeen_) > release || since(rightSeen_) > release ||
                     (firmware && (since(leftOn_) > kApex6DualFirmwareOffGraceBlocks ||
                                   since(rightOn_) > kApex6DualFirmwareOffGraceBlocks)))) {
        episode_ = false;
        preempted_ = Route::Mute;
    }
    const auto changeOwner = [&](Route side, Apex6DualChange change) {
        if (owner_ != side) {
            owner_ = side;
            lastChange_ = stamp;
            decision.change = change;
        }
    };
    if (!episode_) {
        if (!input.identical && (input.leftActive || input.rightActive) &&
            since(leftSeen_) <= kApex6DualEntryWindowBlocks &&
            since(rightSeen_) <= kApex6DualEntryWindowBlocks) {
            episode_ = true;
            auto chosen = leftSince_ > rightSince_ ? Route::Left : Route::Right;
            const bool held = (owner_ == Route::Left || owner_ == Route::Right) && active(owner_) &&
                              since(lastChange_) < kApex6DualOwnerHoldBlocks;
            if (input.leftBurst != input.rightBurst) {
                chosen = input.leftBurst ? Route::Left : Route::Right;
                preempted_ = otherSide(chosen);
            } else if (fresh(Route::Left) != fresh(Route::Right)) {
                chosen = fresh(Route::Left) ? Route::Left : Route::Right;
            } else if (held) {
                chosen = owner_;
            }
            changeOwner(chosen, Apex6DualChange::Episode);
            if (decision.change == Apex6DualChange::None) decision.change = Apex6DualChange::Episode;
        }
    } else {
        const auto other = otherSide(owner_);
        if (burst(other) && active(other) && !burst(owner_)) {
            if (preempted_ == Route::Mute) preempted_ = owner_;
            changeOwner(other, Apex6DualChange::Burst);
        } else if (preempted_ != Route::Mute && !burst(owner_)) {
            const auto back = preempted_;
            preempted_ = Route::Mute;
            changeOwner(back, Apex6DualChange::Return);
        } else if (fresh(other) && active(other) && !burst(owner_)) {
            changeOwner(other, Apex6DualChange::NewEffect);
        } else if (!burst(owner_) && active(other) && seen(owner_) > kApex6DualYieldSilentBlocks &&
                   since(lastChange_) >= kApex6DualOwnerHoldBlocks) {
            changeOwner(other, Apex6DualChange::Yield);
        }
    }
    if (!episode_ || !firmware) {
        firmwareLatched_ = false;
    } else if (!input.leftBurst && !input.rightBurst) {
        firmwareLatched_ = true;
    }
    decision.episode = episode_;
    decision.mix = episode_ && mix;
    decision.firmware = firmwareLatched_;
    decision.owner = owner_;
    if (decision.mix || input.identical) {
        decision.route = Route::Both;
    } else if (episode_) {
        decision.route = owner_;
    } else if (input.rightActive && !input.leftActive) {
        decision.route = Route::Right;
    } else if (input.leftActive) {
        decision.route = Route::Left;
    }
    return decision;
}

Apex6HapticBridge::Apex6HapticBridge(flydigi::Apex5Device& device,
                                     haptics::HapticConfig config,
                                     bool routeGrips, unsigned triggerStrengthPercent,
                                     unsigned vibrationStrengthPercent, unsigned pcmGainPercent,
                                     unsigned gripDuckPercent, unsigned triggerCeilingPercent,
                                     unsigned sampleRateHz)
    : device_(device), hapticProcessor_(apex6HapticConfig(config)), routeGrips_(routeGrips),
      triggerStrengthPercent_(triggerStrengthPercent), vibrationStrengthPercent_(vibrationStrengthPercent),
      pcmGainPercent_(std::clamp(pcmGainPercent, 100U, 200U)),
      gripDuckPercent_((std::min)(gripDuckPercent, 100U)),
      triggerCeilingPercent_(std::clamp(triggerCeilingPercent,
                                        kApex6TriggerCeilingMinimumPercent, 100U)),
      rate2k_(sampleRateHz == 2000), blockSize_(sampleRateHz == 2000 ? 16 : 8) {}

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
    if (!device_.enableApex6Haptics(error, routeGrips_)) return false;
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
        wakeRequested_ = true;
    }
    stopSignal_.notify_all();
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard lock(stateMutex_);
        started_ = false;
        wakeRequested_ = false;
        idle_ = false;
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
        triggerSampleOffset_ = 0;
        leftPosition_ = 0;
        rightPosition_ = 0;
        leftGripPhase_ = 0.0;
        rightGripPhase_ = 0.0;
        leftTriggerPrevious_ = 0;
        rightTriggerPrevious_ = 0;
        leftPcmPrevious_ = 0;
        rightPcmPrevious_ = 0;
        lastLeftRequest_.reset();
        lastRightRequest_.reset();
        dualArbiter_.reset();
        dualBlock_ = 0;
        leftLastEffect_ = {};
        rightLastEffect_ = {};
        leftFirmwareMapping_.reset();
        rightFirmwareMapping_.reset();
        leftEffectSeen_ = leftEffectGeneration_;
        rightEffectSeen_ = rightEffectGeneration_;
        firmwareEngaged_ = false;
        dualFirmwareWanted_ = false;
        dualOwner_ = flydigi::apex6::TriggerRoute::Mute;
        leftFirmwareTarget_.reset();
        rightFirmwareTarget_.reset();
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
    if (feedback.kind == FeedbackKind::AudioHapticWaveform ||
        feedback.kind == FeedbackKind::AudioHapticWaveform2k) {
        if (!routeGrips_) return;
        const bool wide = feedback.kind == FeedbackKind::AudioHapticWaveform2k;
        if (wide && !rate2k_) return;
        waveformBlocks_.fetch_add(1, std::memory_order_relaxed);
        if (wide) waveform2kBlocks_.fetch_add(1, std::memory_order_relaxed);
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
        WaveformBlock block;
        block.sequence = feedback.audioSequence;
        block.receivedAt = Clock::now();
        block.count = wide ? 16 : 8;
        if (wide) {
            block.left = feedback.leftHapticSamples2k;
            block.right = feedback.rightHapticSamples2k;
        } else {
            std::copy(feedback.leftHapticSamples.begin(), feedback.leftHapticSamples.end(),
                      block.left.begin());
            std::copy(feedback.rightHapticSamples.begin(), feedback.rightHapticSamples.end(),
                      block.right.begin());
        }
        auto leftPeak = 0U;
        auto rightPeak = 0U;
        std::uint64_t leftSumSquares = 0;
        std::uint64_t rightSumSquares = 0;
        for (std::size_t index = 0; index < block.count; ++index) {
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
        // This is the game's native haptic PCM, not an audio-energy estimate.
        // A block-level activation gate removes quiet textures and decay tails.
        // Preserve it before the optional render-time gain; the user threshold
        // is bypassed for APEX 6 envelopes too; strength scales final output.
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
        if (idle_ && leftPeak == 0 && rightPeak == 0) {
            silentWaveformSkipped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
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
        wakeLocked();
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
        if (audioEnvelope_.lowFrequency != 0 || audioEnvelope_.highFrequency != 0) wakeLocked();
        return;
    }

    hidReports_.fetch_add(1, std::memory_order_relaxed);
    recordReportSignature(feedback);
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
        if (rumbleLow_ != 0 || rumbleHigh_ != 0) wakeLocked();
    } else if (feedback.rumbleLeft != 0 || feedback.rumbleRight != 0) {
        rumbleIgnoredReports_.fetch_add(1, std::memory_order_relaxed);
        lastIgnoredRumbleEnableBits_.store(feedback.enableBits1, std::memory_order_relaxed);
    }
    if ((feedback.enableBits1 & kLeftTrigger) != 0) {
        handleTrigger(TriggerSide::Left, feedback);
    }
    if ((feedback.enableBits1 & kRightTrigger) != 0) {
        handleTrigger(TriggerSide::Right, feedback);
    }
}

void Apex6HapticBridge::recordReportSignature(const DualSenseFeedback& feedback) {
    const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - traceStartedAt_).count());
    const bool rumble = feedback.rumbleLeft != 0 || feedback.rumbleRight != 0;
    for (std::size_t index = 0; index < reportSignatureCount_; ++index) {
        auto& entry = reportSignatures_[index];
        if (entry.enableBits1 == feedback.enableBits1 && entry.enableBits2 == feedback.enableBits2 &&
            entry.enableBits3 == feedback.enableBits3 && entry.leftMode == feedback.leftTriggerEffect[0] &&
            entry.rightMode == feedback.rightTriggerEffect[0] && entry.rumble == rumble) {
            ++entry.count;
            entry.lastMilliseconds = now;
            return;
        }
    }
    if (reportSignatureCount_ == reportSignatures_.size()) {
        ++reportSignatureOverflow_;
        return;
    }
    auto& entry = reportSignatures_[reportSignatureCount_++];
    entry = {feedback.enableBits1, feedback.enableBits2, feedback.enableBits3,
             feedback.leftTriggerEffect[0], feedback.rightTriggerEffect[0], rumble, 1, now, now};
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
    noteTriggerChangeLocked(side);
    if (next.type != Apex6TriggerType::Off) wakeLocked();
    (left ? leftTriggerState_ : rightTriggerState_).retarget();
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
        noteTriggerChangeLocked(command.side);
        state.reset();
        wakeLocked();
    }
}

void Apex6HapticBridge::setDiagnosticEffect(
    TriggerSide side, const Apex6TriggerEffect& effect) noexcept {
    std::lock_guard lock(stateMutex_);
    auto& target = side == TriggerSide::Left ? leftTrigger_ : rightTrigger_;
    auto& state = side == TriggerSide::Left ? leftTriggerState_ : rightTriggerState_;
    if (target != effect) {
        target = effect;
        noteTriggerChangeLocked(side);
        state.reset();
        wakeLocked();
    }
}

void Apex6HapticBridge::setDiagnosticGrips(
    std::uint8_t left, std::uint8_t right) noexcept {
    std::lock_guard lock(stateMutex_);
    rumbleLow_ = left;
    rumbleHigh_ = right;
    diagnosticGripCarrier_ = true;
    wakeLocked();
}

void Apex6HapticBridge::setDualPolicy(Apex6DualPolicy policy) noexcept {
    std::lock_guard lock(stateMutex_);
    dualPolicy_ = policy;
}

Apex6DualPolicy Apex6HapticBridge::dualPolicy() const noexcept {
    std::lock_guard lock(stateMutex_);
    return dualPolicy_;
}

void Apex6HapticBridge::noteTriggerChangeLocked(TriggerSide side) noexcept {
    const bool left = side == TriggerSide::Left;
    const auto& effect = left ? leftTrigger_ : rightTrigger_;
    if (effect.type == Apex6TriggerType::Off) return;
    auto& last = left ? leftLastEffect_ : rightLastEffect_;
    if (effect == last) return;
    if (effect.type != last.type) ++(left ? leftEffectGeneration_ : rightEffectGeneration_);
    last = effect;
    auto& mapping = left ? leftFirmwareMapping_ : rightFirmwareMapping_;
    mapping = apex6FirmwareEffectFor(effect);
    if (!mapping) dualFirmwareUnmapped_.fetch_add(1, std::memory_order_relaxed);
}

void Apex6HapticBridge::wakeLocked() noexcept {
    idle_ = false;
    wakeRequested_ = true;
    stopSignal_.notify_one();
}

bool Apex6HapticBridge::tryEnterIdle(Clock::time_point now) noexcept {
    std::lock_guard lock(stateMutex_);
    const bool envelopeLive = lastAudioEnvelopeAt_.time_since_epoch().count() != 0 &&
                              now - lastAudioEnvelopeAt_ < kAudioEnvelopeTimeout;
    const bool quiet = leftTrigger_.type == Apex6TriggerType::Off &&
                       rightTrigger_.type == Apex6TriggerType::Off &&
                       rumbleLow_ == 0 && rumbleHigh_ == 0 && !envelopeLive &&
                       std::all_of(waveformQueue_.begin(), waveformQueue_.end(),
                                   [](const WaveformBlock& block) {
                                       return !anyNonZero(block.left) && !anyNonZero(block.right);
                                   });
    if (!quiet) return false;
    silentWaveformSkipped_.fetch_add(waveformQueue_.size(), std::memory_order_relaxed);
    waveformQueue_.clear();
    idle_ = true;
    return true;
}

Apex6HapticBridge::WideBlock Apex6HapticBridge::renderBlock(
    flydigi::apex6::TriggerRoute& route,
    bool& triggerEnabled,
    Clock::time_point now) noexcept {
    const std::size_t count = blockSize_;
    const double rateScale = static_cast<double>(count) / 8.0;
    WideBlock output{};
    std::array<std::int8_t, 8> renderedLeftTrigger;
    std::array<std::int8_t, 8> renderedRightTrigger;
    std::array<std::int8_t, 16> leftTrigger{};
    std::array<std::int8_t, 16> rightTrigger{};
    std::optional<WaveformBlock> waveform;
    haptics::MotorLevels envelope{};
    std::uint8_t rumbleLow = 0;
    std::uint8_t rumbleHigh = 0;
    bool diagnosticGripCarrier = false;
    bool waveformRecentlyReceived = false;
    std::uint8_t leftPositionSnapshot = 0;
    std::uint8_t rightPositionSnapshot = 0;
    bool leftBurstFrame = false;
    bool rightBurstFrame = false;
    Apex6DualInput dualInput{};
    {
        std::lock_guard lock(stateMutex_);
        wakeRequested_ = false;
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
        leftPositionSnapshot = leftPosition_;
        rightPositionSnapshot = rightPosition_;
        const auto leftBurstsBefore = leftTriggerState_.burstsStarted;
        const auto rightBurstsBefore = rightTriggerState_.burstsStarted;
        renderedLeftTrigger = renderApex6TriggerEffect(
            leftTrigger_, leftPosition_, leftTriggerState_, triggerSampleOffset_);
        renderedRightTrigger = renderApex6TriggerEffect(
            rightTrigger_, rightPosition_, rightTriggerState_, triggerSampleOffset_);
        triggerSampleOffset_ += renderedLeftTrigger.size();
        leftBurstFrame = leftTriggerState_.burstInFrame || leftTriggerState_.burstLength != 0;
        rightBurstFrame = rightTriggerState_.burstInFrame || rightTriggerState_.burstLength != 0;
        dualInput.leftEffectOn = leftTrigger_.type != Apex6TriggerType::Off;
        dualInput.rightEffectOn = rightTrigger_.type != Apex6TriggerType::Off;
        dualInput.leftNewEffect = std::exchange(leftEffectSeen_, leftEffectGeneration_) !=
                                  leftEffectGeneration_;
        dualInput.rightNewEffect = std::exchange(rightEffectSeen_, rightEffectGeneration_) !=
                                   rightEffectGeneration_;
        dualInput.firmwareCapable = leftFirmwareMapping_.has_value() &&
                                    rightFirmwareMapping_.has_value();
        dualInput.policy = dualPolicy_;
        if (dualInput.policy == Apex6DualPolicy::Firmware) {
            leftFirmwareTarget_ = leftFirmwareMapping_;
            rightFirmwareTarget_ = rightFirmwareMapping_;
        }
        if (leftTriggerState_.burstsStarted > leftBurstsBefore) {
            (leftTrigger_.type == Apex6TriggerType::Bow ? bowBreaks_ : weaponBreaks_)
                .fetch_add(1, std::memory_order_relaxed);
        }
        if (rightTriggerState_.burstsStarted > rightBurstsBefore) {
            (rightTrigger_.type == Apex6TriggerType::Bow ? bowBreaks_ : weaponBreaks_)
                .fetch_add(1, std::memory_order_relaxed);
        }
    }

    expandBlock(renderedLeftTrigger, leftTriggerPrevious_, count, leftTrigger);
    expandBlock(renderedRightTrigger, rightTriggerPrevious_, count, rightTrigger);

    std::array<std::int16_t, 16> leftPcm{};
    std::array<std::int16_t, 16> rightPcm{};
    if (waveform) {
        if (waveform->count == count) {
            leftPcm = waveform->left;
            rightPcm = waveform->right;
            leftPcmPrevious_ = leftPcm[count - 1];
            rightPcmPrevious_ = rightPcm[count - 1];
        } else if (waveform->count == 8 && count == 16) {
            std::array<std::int16_t, 8> leftNarrow;
            std::array<std::int16_t, 8> rightNarrow;
            std::copy_n(waveform->left.begin(), 8, leftNarrow.begin());
            std::copy_n(waveform->right.begin(), 8, rightNarrow.begin());
            expandBlock(leftNarrow, leftPcmPrevious_, count, leftPcm);
            expandBlock(rightNarrow, rightPcmPrevious_, count, rightPcm);
            waveformUpsampledBlocks_.fetch_add(1, std::memory_order_relaxed);
        }
    } else {
        leftPcmPrevious_ = 0;
        rightPcmPrevious_ = 0;
    }
    const auto nonZero = [](std::int16_t value) { return value != 0; };
    const bool leftPcmActive = std::any_of(leftPcm.begin(), leftPcm.begin() + count, nonZero);
    const bool rightPcmActive = std::any_of(rightPcm.begin(), rightPcm.begin() + count, nonZero);
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
    if (routeGrips_ && ((rumbleLow != 0 && !leftRumble) || (rumbleHigh != 0 && !rightRumble))) {
        gripRumbleMaskedFrames_.fetch_add(1, std::memory_order_relaxed);
    }
    for (std::size_t index = 0; routeGrips_ && index < count; ++index) {
        if (leftPcmActive) {
            output[index].leftGrip = scaleApex6Pcm(leftPcm[index], pcmGainPercent_);
        } else if (leftEnvelope) {
            output[index].leftGrip = sineSample(
                leftGripPhase_, 85.0 / rateScale, envelope.lowFrequency);
        } else if (leftRumble) {
            output[index].leftGrip = sineSample(
                leftGripPhase_, (diagnosticGripCarrier ? 120.0 : 65.0) / rateScale,
                rumbleLow);
        }
        if (rightPcmActive) {
            output[index].rightGrip = scaleApex6Pcm(rightPcm[index], pcmGainPercent_);
        } else if (rightEnvelope) {
            output[index].rightGrip = sineSample(
                rightGripPhase_, 85.0 / rateScale, envelope.highFrequency);
        } else if (rightRumble) {
            output[index].rightGrip = sineSample(
                rightGripPhase_, (diagnosticGripCarrier ? 120.0 : 150.0) / rateScale,
                rumbleHigh);
        }
    }

    unsigned leftPcmOutputPeak = 0;
    unsigned rightPcmOutputPeak = 0;
    const auto gripDuckGain = apex6GripDuckGainPermille(
        leftPositionSnapshot, rightPositionSnapshot, gripDuckPercent_);
    bool gripDucked = false;
    std::uint64_t triggerLimited = 0;
    for (std::size_t index = 0; index < count; ++index) {
        output[index].leftGrip = scaleEffectStrength(output[index].leftGrip, vibrationStrengthPercent_);
        output[index].rightGrip = scaleEffectStrength(output[index].rightGrip, vibrationStrengthPercent_);
        if (gripDuckGain != kApex6GripNoDuckPermille) {
            const auto duckedLeft = applyApex6GripDuck(output[index].leftGrip, gripDuckGain);
            const auto duckedRight = applyApex6GripDuck(output[index].rightGrip, gripDuckGain);
            gripDucked = gripDucked || duckedLeft != output[index].leftGrip ||
                         duckedRight != output[index].rightGrip;
            output[index].leftGrip = duckedLeft;
            output[index].rightGrip = duckedRight;
        }
        if (routeGrips_ && leftPcmActive) {
            leftPcmOutputPeak = (std::max)(leftPcmOutputPeak,
                static_cast<unsigned>(std::abs(int(output[index].leftGrip))));
        }
        if (routeGrips_ && rightPcmActive) {
            rightPcmOutputPeak = (std::max)(rightPcmOutputPeak,
                static_cast<unsigned>(std::abs(int(output[index].rightGrip))));
        }
        const auto scaledLeftTrigger = scaleEffectStrength(leftTrigger[index], triggerStrengthPercent_);
        const auto scaledRightTrigger = scaleEffectStrength(rightTrigger[index], triggerStrengthPercent_);
        leftTrigger[index] = limitApex6TriggerSample(scaledLeftTrigger, triggerCeilingPercent_);
        rightTrigger[index] = limitApex6TriggerSample(scaledRightTrigger, triggerCeilingPercent_);
        if (leftTrigger[index] != scaledLeftTrigger) ++triggerLimited;
        if (rightTrigger[index] != scaledRightTrigger) ++triggerLimited;
    }
    if (gripDucked) gripDuckedBlocks_.fetch_add(1, std::memory_order_relaxed);
    if (triggerLimited != 0) triggerLimitedSamples_.fetch_add(triggerLimited, std::memory_order_relaxed);
    if (routeGrips_ && leftPcmActive) updateMaximum(pcmOutputLeftPeak_, leftPcmOutputPeak);
    if (routeGrips_ && rightPcmActive) updateMaximum(pcmOutputRightPeak_, rightPcmOutputPeak);
    if ((leftPcmActive || rightPcmActive) &&
        std::any_of(output.begin(), output.begin() + count, [leftPcmActive, rightPcmActive](const auto& sample) {
            return (leftPcmActive && sample.leftGrip != 0) ||
                   (rightPcmActive && sample.rightGrip != 0);
        })) {
        waveformActiveRendered_.fetch_add(1, std::memory_order_relaxed);
    }
    const bool leftActive = anyNonZero(leftTrigger);
    const bool rightActive = anyNonZero(rightTrigger);
    dualInput.block = dualBlock_++;
    dualInput.leftActive = leftActive;
    dualInput.rightActive = rightActive;
    dualInput.identical = leftActive && rightActive && leftTrigger == rightTrigger;
    dualInput.leftBurst = leftBurstFrame && leftActive;
    dualInput.rightBurst = rightBurstFrame && rightActive;
    const auto decision = dualArbiter_.next(dualInput);
    dualOwner_ = decision.owner;
    dualFirmwareWanted_ = decision.firmware;
    if (decision.episode) dualTriggerBlocks_.fetch_add(1, std::memory_order_relaxed);
    switch (decision.change) {
    case Apex6DualChange::None: break;
    case Apex6DualChange::Episode: dualEpisodes_.fetch_add(1, std::memory_order_relaxed); break;
    case Apex6DualChange::Burst: dualBurstSwitches_.fetch_add(1, std::memory_order_relaxed); break;
    case Apex6DualChange::Return: dualReturns_.fetch_add(1, std::memory_order_relaxed); break;
    case Apex6DualChange::NewEffect: dualOnsetSwitches_.fetch_add(1, std::memory_order_relaxed); break;
    case Apex6DualChange::Yield: dualYieldSwitches_.fetch_add(1, std::memory_order_relaxed); break;
    }
    renderedSide_ = flydigi::apex6::TriggerRoute::Mute;
    route = flydigi::apex6::TriggerRoute::Mute;
    if (firmwareEngaged_) {
        triggerEnabled = false;
        dualFirmwareBlocks_.fetch_add(1, std::memory_order_relaxed);
        return output;
    }
    triggerEnabled = leftActive || rightActive;
    if (!triggerEnabled) return output;
    route = decision.route;
    renderedSide_ = route;
    if (route == flydigi::apex6::TriggerRoute::Both && decision.mix && !dualInput.identical) {
        std::uint64_t clipped = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const int sum = static_cast<int>(leftTrigger[index]) + static_cast<int>(rightTrigger[index]);
            const int limited = std::clamp(sum, -kApex6DualMixPeak, kApex6DualMixPeak);
            if (limited != sum) ++clipped;
            output[index].trigger = static_cast<std::int8_t>(limited);
        }
        if (clipped != 0) dualMixClippedSamples_.fetch_add(clipped, std::memory_order_relaxed);
        dualMixFrames_.fetch_add(1, std::memory_order_relaxed);
        return output;
    }
    if (route == flydigi::apex6::TriggerRoute::Both) {
        for (std::size_t index = 0; index < count; ++index) {
            output[index].trigger = leftTrigger[index];
        }
        bothTriggerFrames_.fetch_add(1, std::memory_order_relaxed);
        return output;
    }
    const bool chooseRight = route == flydigi::apex6::TriggerRoute::Right;
    const auto& selected = chooseRight ? rightTrigger : leftTrigger;
    for (std::size_t index = 0; index < count; ++index) {
        output[index].trigger = selected[index];
    }
    (chooseRight ? rightTriggerFrames_ : leftTriggerFrames_)
        .fetch_add(1, std::memory_order_relaxed);
    return output;
}

void Apex6HapticBridge::run() noexcept {
    using Route = flydigi::apex6::TriggerRoute;
    using FirmwareEffect = flydigi::apex6::TriggerFirmwareEffect;
    auto deadline = Clock::now();
#ifdef _WIN32
    HighResolutionDeadlineTimer deadlineTimer;
#endif
    unsigned silentStreak = 0;
    unsigned triggerReleaseFrames = 0;
    bool needsPrime = rate2k_;
    auto triggerTarget = Route::Both;
    std::array<std::optional<FirmwareEffect>, 2> firmwareWritten{};
    std::array<std::uint64_t, 2> firmwareWrittenAt{};
    std::uint64_t period = 0;
    const auto sideIndex = [](Route side) { return side == Route::Right ? std::size_t{1} : std::size_t{0}; };
    const auto forgetFirmware = [&firmwareWritten, &sideIndex](Route side) {
        if (side == Route::Both) {
            firmwareWritten = {};
        } else if (singleSide(side)) {
            firmwareWritten[sideIndex(side)].reset();
        }
    };
    const auto writeReport = [this](const flydigi::apex6::MotorBlock& half,
                                    bool trigger,
                                    flydigi::apex6::TriggerRoute halfRoute,
                                    bool interpolate) {
        std::string writeError;
        const auto writeStartedAt = Clock::now();
        if (!device_.writeApex6Haptics(
                half, halfRoute, trigger, routeGrips_, writeError, interpolate)) {
            writeFailures_.fetch_add(1, std::memory_order_relaxed);
            recordError(std::move(writeError));
            return false;
        }
        const auto writeDurationUs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - writeStartedAt).count());
        totalWriteDurationUs_.fetch_add(writeDurationUs, std::memory_order_relaxed);
        updateMaximum(maximumWriteDurationUs_, writeDurationUs);
        framesWritten_.fetch_add(1, std::memory_order_relaxed);
        return true;
    };
    const auto slackAllowsWrite = [this, &deadline] {
        const auto frames = framesWritten_.load(std::memory_order_relaxed);
        const auto averageWriteUs = frames == 0 ? 0
            : totalWriteDurationUs_.load(std::memory_order_relaxed) / frames;
        const auto budget = std::chrono::microseconds(
            (std::max)(averageWriteUs + kSlackWriteMarginUs, kSlackWriteMinimumUs));
        return deadline - Clock::now() >= budget;
    };
    const auto reconcileFirmware = [&]() {
        enum class Action : std::uint8_t { None, Program, Restore, Clear };
        for (unsigned writes = 0; writes < kApex6DualMaximumSlackWrites; ++writes) {
            auto action = Action::None;
            auto side = Route::Mute;
            bool refresh = false;
            std::optional<FirmwareEffect> desired;
            const auto owner = dualOwner_;
            if (dualFirmwareWanted_) {
                const auto first = owner == Route::Left ? Route::Right : Route::Left;
                for (const auto candidate : {first, otherSide(first)}) {
                    const auto& target = candidate == Route::Left ? leftFirmwareTarget_ : rightFirmwareTarget_;
                    const auto& written = firmwareWritten[sideIndex(candidate)];
                    if (!target) continue;
                    if (!written || (*written != *target &&
                                     period - firmwareWrittenAt[sideIndex(candidate)] >=
                                         kApex6DualFirmwareRefreshBlocks)) {
                        action = Action::Program;
                        side = candidate;
                        refresh = written.has_value();
                        desired = target;
                        break;
                    }
                }
            } else if (firmwareWritten[0] && firmwareWritten[1]) {
                action = Action::Restore;
                side = singleSide(owner) ? owner : Route::Both;
            } else if (firmwareWritten[0] || firmwareWritten[1]) {
                action = Action::Clear;
                side = firmwareWritten[0] ? Route::Left : Route::Right;
            }
            if (action == Action::None) return true;
            if (!slackAllowsWrite()) {
                dualFirmwareDeferred_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            std::string writeError;
            const auto writeStartedAt = Clock::now();
            const bool written = action == Action::Program
                ? device_.writeApex6TriggerFirmwareEffect(side, *desired, writeError)
                : device_.writeApex6TriggerEnable(side, action == Action::Restore, writeError);
            dualFirmwareWrites_.fetch_add(1, std::memory_order_relaxed);
            dualFirmwareWriteUs_.fetch_add(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    Clock::now() - writeStartedAt).count()), std::memory_order_relaxed);
            if (!written) {
                writeFailures_.fetch_add(1, std::memory_order_relaxed);
                recordError(std::move(writeError));
                return false;
            }
            if (action == Action::Program) {
                const bool wasEngaged = firmwareWritten[0] && firmwareWritten[1];
                firmwareWritten[sideIndex(side)] = desired;
                firmwareWrittenAt[sideIndex(side)] = period;
                if (refresh) dualFirmwareRefreshes_.fetch_add(1, std::memory_order_relaxed);
                if (triggerTarget == side) {
                    triggerTarget = Route::Mute;
                } else if (triggerTarget == Route::Both) {
                    triggerTarget = otherSide(side);
                }
                if (!wasEngaged && firmwareWritten[0] && firmwareWritten[1]) {
                    dualFirmwareEntries_.fetch_add(1, std::memory_order_relaxed);
                }
            } else if (action == Action::Restore) {
                forgetFirmware(side);
                triggerTarget = side;
                dualFirmwareExits_.fetch_add(1, std::memory_order_relaxed);
            } else {
                forgetFirmware(side);
            }
        }
        return true;
    };
#ifdef _WIN32
    const auto waitUntilOrStop = [this, &deadlineTimer](Clock::time_point target) {
        const bool usedHighResolutionTimer = deadlineTimer.waitUntil(target);
        std::unique_lock lock(stateMutex_);
        if (usedHighResolutionTimer) return !stopping_;
        return !stopSignal_.wait_until(lock, target, [this] { return stopping_; });
    };
#else
    const auto waitUntilOrStop = [this](Clock::time_point target) {
        std::unique_lock lock(stateMutex_);
        return !stopSignal_.wait_until(lock, target, [this] { return stopping_; });
    };
#endif
    for (;;) {
        deadline += kFramePeriod;
        ++period;
        Route route{};
        bool triggerEnabled = false;
        const auto renderedAt = Clock::now();
        const auto block = renderBlock(route, triggerEnabled, renderedAt);
        const auto renderNs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - renderedAt).count());
        renderedBlocks_.fetch_add(1, std::memory_order_relaxed);
        renderTotalNs_.fetch_add(renderNs, std::memory_order_relaxed);
        updateMaximum(renderMaximumNs_, renderNs);
        if (triggerEnabled && triggerTarget == Route::Mute && renderedSide_ != Route::Mute) {
            std::string retargetError;
            if (!device_.writeApex6TriggerEnable(renderedSide_, true, retargetError)) {
                writeFailures_.fetch_add(1, std::memory_order_relaxed);
                recordError(std::move(retargetError));
                return;
            }
            forgetFirmware(renderedSide_);
            triggerTarget = renderedSide_;
            triggerRetargets_.fetch_add(1, std::memory_order_relaxed);
        }
        if (triggerEnabled && renderedSide_ != Route::Mute && renderedSide_ != triggerTarget) {
            std::string retargetError;
            if (!device_.setApex6TriggerSide(renderedSide_, retargetError)) {
                writeFailures_.fetch_add(1, std::memory_order_relaxed);
                recordError(std::move(retargetError));
                return;
            }
            forgetFirmware(Route::Both);
            if (singleSide(triggerTarget) && singleSide(renderedSide_)) {
                dualOwnerChanges_.fetch_add(1, std::memory_order_relaxed);
            }
            triggerTarget = renderedSide_;
            triggerRetargets_.fetch_add(1, std::memory_order_relaxed);
        }
        firmwareEngaged_ = firmwareWritten[0] && firmwareWritten[1];
        if (triggerEnabled) {
            triggerReleaseFrames = kTriggerReleaseFrames;
        } else if (triggerReleaseFrames != 0 && !firmwareEngaged_) {
            --triggerReleaseFrames;
            triggerEnabled = true;
            route = Route::Both;
        }
        const bool silent = !triggerEnabled &&
            std::all_of(block.begin(), block.begin() + blockSize_, [](const auto& sample) {
                return sample.leftGrip == 0 && sample.rightGrip == 0 && sample.trigger == 0;
            });
        silentStreak = silent ? silentStreak + 1 : 0;
        const bool firmwareIdle = !firmwareWritten[0] && !firmwareWritten[1];
        bool keepalive = false;
        if (silentStreak >= kIdleEnterFrames && firmwareIdle && tryEnterIdle(renderedAt)) {
            needsPrime = rate2k_;
            std::unique_lock lock(stateMutex_);
            const bool woken = stopSignal_.wait_for(
                lock, kIdleKeepalive, [this] { return wakeRequested_; });
            if (stopping_) return;
            idle_ = false;
            lock.unlock();
            const auto now = Clock::now();
            idleFramesSkipped_.fetch_add(
                static_cast<std::uint64_t>((now - renderedAt) / kFramePeriod),
                std::memory_order_relaxed);
            deadline = now;
            if (woken) {
                silentStreak = 0;
                continue;
            }
            keepalive = true;
            idleKeepalives_.fetch_add(1, std::memory_order_relaxed);
        }
        flydigi::apex6::MotorBlock firstHalf;
        std::copy_n(block.begin(), firstHalf.size(), firstHalf.begin());
        if (rate2k_ && needsPrime && !keepalive) {
            const flydigi::apex6::MotorBlock silentHalf{};
            for (unsigned primed = 0; primed < kPrimeHalfFrames2k; ++primed) {
                if (!writeReport(silentHalf, false, Route::Mute, false)) return;
                primeReports_.fetch_add(1, std::memory_order_relaxed);
            }
            needsPrime = false;
        }
        if (!writeReport(firstHalf, triggerEnabled, route, !rate2k_)) return;
        if (rate2k_) {
            flydigi::apex6::MotorBlock secondHalf;
            std::copy_n(block.begin() + firstHalf.size(), secondHalf.size(), secondHalf.begin());
            if (!waitUntilOrStop(deadline - kHalfFramePeriod)) return;
            if (!writeReport(secondHalf, triggerEnabled, route, false)) return;
        }
        if (keepalive) continue;
        if ((dualFirmwareWanted_ || firmwareWritten[0] || firmwareWritten[1]) && !reconcileFirmware()) {
            return;
        }
        firmwareEngaged_ = firmwareWritten[0] && firmwareWritten[1];
        if (firmwareEngaged_) triggerReleaseFrames = 0;

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
    result.bowBreaks = bowBreaks_.load(std::memory_order_relaxed);
    result.lastLeftTriggerType = lastLeftTriggerType_.load(std::memory_order_relaxed);
    result.lastRightTriggerType = lastRightTriggerType_.load(std::memory_order_relaxed);
    result.rumbleUpdates = rumbleUpdates_.load(std::memory_order_relaxed);
    result.rumbleActiveUpdates = rumbleActiveUpdates_.load(std::memory_order_relaxed);
    result.rumbleIgnoredReports = rumbleIgnoredReports_.load(std::memory_order_relaxed);
    result.lastIgnoredRumbleEnableBits =
        lastIgnoredRumbleEnableBits_.load(std::memory_order_relaxed);
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
            (static_cast<double>(result.waveformLeftActiveBlocks) *
             static_cast<double>(blockSize_)));
    }
    if (result.waveformRightActiveBlocks != 0) {
        result.waveformRightActiveRms = std::sqrt(static_cast<double>(
            waveformRightSumSquares_.load(std::memory_order_relaxed)) /
            (static_cast<double>(result.waveformRightActiveBlocks) *
             static_cast<double>(blockSize_)));
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
    result.pcmGainPercent = pcmGainPercent_;
    result.gripDuckPercent = gripDuckPercent_;
    result.triggerCeilingPercent = triggerCeilingPercent_;
    result.gripDuckedBlocks = gripDuckedBlocks_.load(std::memory_order_relaxed);
    result.triggerLimitedSamples = triggerLimitedSamples_.load(std::memory_order_relaxed);
    result.pcmOutputLeftPeak = pcmOutputLeftPeak_.load(std::memory_order_relaxed);
    result.pcmOutputRightPeak = pcmOutputRightPeak_.load(std::memory_order_relaxed);
    result.gripEnvelopeFrames = gripEnvelopeFrames_.load(std::memory_order_relaxed);
    result.gripRumbleFrames = gripRumbleFrames_.load(std::memory_order_relaxed);
    result.gripRumbleMaskedFrames = gripRumbleMaskedFrames_.load(std::memory_order_relaxed);
    result.deadlineOverruns = deadlineOverruns_.load(std::memory_order_relaxed);
    result.idleFramesSkipped = idleFramesSkipped_.load(std::memory_order_relaxed);
    result.idleKeepalives = idleKeepalives_.load(std::memory_order_relaxed);
    result.silentWaveformSkipped = silentWaveformSkipped_.load(std::memory_order_relaxed);
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
    result.triggerRetargets = triggerRetargets_.load(std::memory_order_relaxed);
    result.renderedBlocks = renderedBlocks_.load(std::memory_order_relaxed);
    result.renderTotalNs = renderTotalNs_.load(std::memory_order_relaxed);
    result.renderMaximumNs = renderMaximumNs_.load(std::memory_order_relaxed);
    result.dualTriggerBlocks = dualTriggerBlocks_.load(std::memory_order_relaxed);
    result.dualEpisodes = dualEpisodes_.load(std::memory_order_relaxed);
    result.dualBurstSwitches = dualBurstSwitches_.load(std::memory_order_relaxed);
    result.dualOnsetSwitches = dualOnsetSwitches_.load(std::memory_order_relaxed);
    result.dualYieldSwitches = dualYieldSwitches_.load(std::memory_order_relaxed);
    result.dualMixFrames = dualMixFrames_.load(std::memory_order_relaxed);
    result.dualMixClippedSamples = dualMixClippedSamples_.load(std::memory_order_relaxed);
    result.dualReturns = dualReturns_.load(std::memory_order_relaxed);
    result.dualOwnerChanges = dualOwnerChanges_.load(std::memory_order_relaxed);
    result.dualFirmwareBlocks = dualFirmwareBlocks_.load(std::memory_order_relaxed);
    result.dualFirmwareEntries = dualFirmwareEntries_.load(std::memory_order_relaxed);
    result.dualFirmwareExits = dualFirmwareExits_.load(std::memory_order_relaxed);
    result.dualFirmwareRefreshes = dualFirmwareRefreshes_.load(std::memory_order_relaxed);
    result.dualFirmwareWrites = dualFirmwareWrites_.load(std::memory_order_relaxed);
    result.dualFirmwareWriteUs = dualFirmwareWriteUs_.load(std::memory_order_relaxed);
    result.dualFirmwareDeferred = dualFirmwareDeferred_.load(std::memory_order_relaxed);
    result.dualFirmwareUnmapped = dualFirmwareUnmapped_.load(std::memory_order_relaxed);
    result.hapticEnables = hapticEnables_.load(std::memory_order_relaxed);
    result.hapticDisables = hapticDisables_.load(std::memory_order_relaxed);
    result.sampleRateHz = rate2k_ ? 2000U : 1000U;
    result.waveform2kBlocks = waveform2kBlocks_.load(std::memory_order_relaxed);
    result.waveformUpsampledBlocks = waveformUpsampledBlocks_.load(std::memory_order_relaxed);
    result.primeReports = primeReports_.load(std::memory_order_relaxed);
    {
        std::lock_guard lock(stateMutex_);
        result.lastActiveLeft = lastActiveLeft_;
        result.lastActiveRight = lastActiveRight_;
        result.lastRejectedLeft = lastRejectedLeft_;
        result.lastRejectedRight = lastRejectedRight_;
        result.triggerTraceCount = triggerTraceCount_;
        result.triggerTraceOverwritten = triggerTraceOverwritten_;
        result.reportSignatures = reportSignatures_;
        result.reportSignatureCount = reportSignatureCount_;
        result.reportSignatureOverflow = reportSignatureOverflow_;
        result.dualPolicy = dualPolicy_;
        const auto first = triggerTraceCount_ == triggerTrace_.size() ? triggerTraceNext_ : 0;
        for (std::size_t index = 0; index < triggerTraceCount_; ++index) {
            result.triggerTrace[index] = triggerTrace_[(first + index) % triggerTrace_.size()];
        }
    }
    return result;
}

} // namespace asb::dualsense
