#include "dualsense/Apex6HapticBridge.h"

#include "dualsense/AdaptiveTriggerTranslation.h"

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
constexpr std::int16_t kMaximumTriggerDrive = 96;
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

bool anyNonZero(const std::array<std::int8_t, 8>& samples) noexcept {
    return std::any_of(samples.begin(), samples.end(),
                       [](std::int8_t sample) { return sample != 0; });
}

std::optional<ForceTriggerCommand> translateApex6Trigger(
    TriggerSide side,
    const std::array<std::uint8_t, 11>& effect,
    std::uint8_t fallbackMotor) noexcept {
    constexpr std::uint8_t kFeedback = 0x21;
    constexpr std::uint8_t kWeapon = 0x25;
    constexpr std::uint8_t kVibration = 0x26;
    if (effect[0] != kFeedback && effect[0] != kWeapon &&
        effect[0] != kVibration) {
        return translateAdaptiveTrigger(side, effect, fallbackMotor);
    }

    const auto mask = static_cast<std::uint16_t>(effect[1]) |
                      (static_cast<std::uint16_t>(effect[2]) << 8U);
    unsigned firstZone = 10;
    unsigned lastZone = 0;
    for (unsigned zone = 0; zone < 10; ++zone) {
        if ((mask & (std::uint16_t{1} << zone)) == 0) continue;
        firstZone = (std::min)(firstZone, zone);
        lastZone = zone;
    }
    if (firstZone == 10) return std::nullopt;

    std::uint8_t strength = 0;
    for (std::size_t index = 3; index < effect.size(); ++index) {
        strength = (std::max)(strength, effect[index]);
    }
    if (strength == 0) strength = 48;
    const auto start = static_cast<std::uint8_t>(
        (firstZone * 255U + 5U) / 10U);
    ForceTriggerCommand result{};
    result.side = side;
    if (effect[0] == kFeedback) {
        result.mode = TriggerMode::Race;
        result.params = {start, strength, 0, 0, 0};
    } else if (effect[0] == kWeapon) {
        result.mode = TriggerMode::SniperBreak;
        (void)lastZone;
        result.params = {start, strength, strength, 0, 0};
    } else {
        result.mode = TriggerMode::Vibration;
        const auto frequency = effect[10] == 0 ? std::uint8_t{65} : effect[10];
        result.params = {start, 1, strength, frequency, 0};
    }
    return result;
}

} // namespace

Apex6HapticBridge::Apex6HapticBridge(flydigi::Apex5Device& device,
                                     haptics::HapticConfig config,
                                     bool routeGrips)
    : device_(device), hapticProcessor_(config), routeGrips_(routeGrips) {}

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
        latestWaveform_.reset();
    }
    std::string disableError;
    const bool disabled = device_.disableApex6Haptics(disableError);
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
        WaveformBlock block{feedback.audioSequence,
                            feedback.leftHapticSamples,
                            feedback.rightHapticSamples};
        if (latestWaveform_) {
            waveformBlocksDropped_.fetch_add(1, std::memory_order_relaxed);
        }
        latestWaveform_ = std::move(block);
        waveformBlocks_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (feedback.kind == FeedbackKind::AudioHaptics) {
        if (!routeGrips_) return;
        audioEnvelope_ = hapticProcessor_.process(feedback);
        lastAudioEnvelopeAt_ = Clock::now();
        return;
    }

    constexpr std::uint8_t kCompatibleVibration = 0x01;
    constexpr std::uint8_t kCompatibleVibration2 = 0x04;
    constexpr std::uint8_t kRightTrigger = 0x04;
    constexpr std::uint8_t kLeftTrigger = 0x08;
    if ((feedback.enableBits1 & kCompatibleVibration) != 0 ||
        (feedback.enableBits3 & kCompatibleVibration2) != 0) {
        rumbleLow_ = routeGrips_ ? feedback.rumbleLeft : 0;
        rumbleHigh_ = routeGrips_ ? feedback.rumbleRight : 0;
        diagnosticGripCarrier_ = false;
    }
    if ((feedback.enableBits1 & kLeftTrigger) != 0) {
        const auto translated = translateApex6Trigger(
            TriggerSide::Left, feedback.leftTriggerEffect, rumbleLow_);
        if (translated) leftTrigger_ = translated;
    }
    if ((feedback.enableBits1 & kRightTrigger) != 0) {
        const auto translated = translateApex6Trigger(
            TriggerSide::Right, feedback.rightTriggerEffect, rumbleLow_);
        if (translated) rightTrigger_ = translated;
    }
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
    (command.side == TriggerSide::Left ? leftTrigger_ : rightTrigger_) = command;
}

void Apex6HapticBridge::setDiagnosticGrips(
    std::uint8_t left, std::uint8_t right) noexcept {
    std::lock_guard lock(stateMutex_);
    rumbleLow_ = left;
    rumbleHigh_ = right;
    diagnosticGripCarrier_ = true;
}

std::array<std::int8_t, 8> Apex6HapticBridge::renderTrigger(
    const std::optional<ForceTriggerCommand>& command,
    std::uint8_t position,
    double& phase) noexcept {
    std::array<std::int8_t, 8> result{};
    if (!command || command->mode == TriggerMode::Normal) return result;

    const auto start = command->params[0];
    if (position < start) return result;
    const auto rawStrength = [&] {
        switch (command->mode) {
        case TriggerMode::Race:
            return command->params[1];
        case TriggerMode::SniperBreak:
        case TriggerMode::RecoilRattle:
        case TriggerMode::Vibration:
            return (std::max)({command->params[1], command->params[2],
                               command->params[3]});
        case TriggerMode::Lock:
            return std::uint8_t{255};
        case TriggerMode::Normal:
            return std::uint8_t{0};
        }
        return std::uint8_t{0};
    }();
    const auto strength = std::clamp<std::int16_t>(
        rawStrength, 1, kMaximumTriggerDrive);

    // Apex 6 trigger actuators are vibration voice coils, not the geared
    // FORCEADAPT mechanism used by Apex 4/5. A DC level only produces a tiny
    // mechanical notch. Represent every active DualSense trigger effect with
    // a bipolar carrier and retain the requested frequency for vibration
    // effects.
    double frequency = 120.0;
    if (command->mode == TriggerMode::SniperBreak) {
        frequency = 150.0;
    } else if (command->mode == TriggerMode::RecoilRattle ||
               command->mode == TriggerMode::Vibration) {
        frequency = std::clamp<double>(
            command->params[3] == 0 ? 120.0 : command->params[3], 40.0, 200.0);
    }
    for (auto& sample : result) {
        const auto value = static_cast<std::int32_t>(
            std::lround(std::sin(phase * 2.0 * kPi) * strength));
        sample = static_cast<std::int8_t>(
            std::clamp<std::int32_t>(value, -kMaximumTriggerDrive,
                                     kMaximumTriggerDrive));
        phase += frequency / 1000.0;
        phase -= std::floor(phase);
    }
    return result;
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
    {
        std::lock_guard lock(stateMutex_);
        if (latestWaveform_) {
            waveform = std::move(latestWaveform_);
            latestWaveform_.reset();
            waveformBlocksRendered_.fetch_add(1, std::memory_order_relaxed);
        }
        if (lastAudioEnvelopeAt_.time_since_epoch().count() != 0 &&
            now - lastAudioEnvelopeAt_ < kAudioEnvelopeTimeout) {
            envelope = audioEnvelope_;
        }
        rumbleLow = rumbleLow_;
        rumbleHigh = rumbleHigh_;
        diagnosticGripCarrier = diagnosticGripCarrier_;
        leftTrigger = renderTrigger(leftTrigger_, leftPosition_, leftTriggerPhase_);
        rightTrigger = renderTrigger(rightTrigger_, rightPosition_, rightTriggerPhase_);
    }

    for (std::size_t index = 0; routeGrips_ && index < output.size(); ++index) {
        if (waveform) {
            output[index].leftGrip = scalePcm(waveform->left[index]);
            output[index].rightGrip = scalePcm(waveform->right[index]);
        } else if (envelope.lowFrequency != 0 || envelope.highFrequency != 0) {
            output[index].leftGrip = sineSample(
                leftGripPhase_, 85.0, envelope.lowFrequency);
            output[index].rightGrip = sineSample(
                rightGripPhase_, 85.0, envelope.highFrequency);
        } else {
            const auto leftFrequency = diagnosticGripCarrier ? 120.0 : 65.0;
            const auto rightFrequency = diagnosticGripCarrier ? 120.0 : 150.0;
            output[index].leftGrip = sineSample(
                leftGripPhase_, leftFrequency, rumbleLow);
            output[index].rightGrip = sineSample(
                rightGripPhase_, rightFrequency, rumbleHigh);
        }
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
        auto maximumWriteUs = maximumWriteDurationUs_.load(std::memory_order_relaxed);
        while (writeDurationUs > maximumWriteUs &&
               !maximumWriteDurationUs_.compare_exchange_weak(
                   maximumWriteUs, writeDurationUs, std::memory_order_relaxed)) {
        }
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
    return {framesWritten_.load(std::memory_order_relaxed),
            waveformBlocks_.load(std::memory_order_relaxed),
            waveformBlocksRendered_.load(std::memory_order_relaxed),
            waveformBlocksDropped_.load(std::memory_order_relaxed),
            deadlineOverruns_.load(std::memory_order_relaxed),
            writeFailures_.load(std::memory_order_relaxed),
            totalWriteDurationUs_.load(std::memory_order_relaxed),
            maximumWriteDurationUs_.load(std::memory_order_relaxed),
            leftTriggerFrames_.load(std::memory_order_relaxed),
            rightTriggerFrames_.load(std::memory_order_relaxed),
            bothTriggerFrames_.load(std::memory_order_relaxed)};
}

} // namespace asb::dualsense
