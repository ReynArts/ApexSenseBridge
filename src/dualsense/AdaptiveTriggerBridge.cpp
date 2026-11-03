#include "dualsense/AdaptiveTriggerBridge.h"

#include "dualsense/AdaptiveTriggerTranslation.h"
#include "dualsense/EffectStrength.h"

#include <algorithm>
#include <cstdio>
#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace asb::dualsense {
namespace {

constexpr std::chrono::milliseconds kPeriodicTick{2};
constexpr std::chrono::milliseconds kPeriodicMinimumSpacing{10};

class DeadlineTimer {
public:
#ifdef _WIN32
    DeadlineTimer() noexcept {
        constexpr DWORD kHighResolution = 0x00000002;
        timer_ = CreateWaitableTimerExW(
            nullptr, nullptr, kHighResolution, TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!timer_) {
            timer_ = CreateWaitableTimerExW(
                nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
        }
    }

    ~DeadlineTimer() {
        if (timer_) CloseHandle(timer_);
    }

    void waitUntil(std::chrono::steady_clock::time_point deadline) noexcept {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) return;
        if (timer_) {
            const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                remaining).count();
            LARGE_INTEGER due{};
            due.QuadPart = -static_cast<LONGLONG>((nanoseconds + 99) / 100);
            if (due.QuadPart == 0) due.QuadPart = -1;
            if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer_, INFINITE);
                return;
            }
        }
        std::this_thread::sleep_until(deadline);
    }

private:
    HANDLE timer_ = nullptr;
#else
    void waitUntil(std::chrono::steady_clock::time_point deadline) noexcept {
        std::this_thread::sleep_until(deadline);
    }
#endif
};
constexpr unsigned kPeriodicAmplitudeStep = 3;
constexpr unsigned kPeriodicPressOn = 8;
constexpr unsigned kPeriodicPressOff = 3;

bool pressedWithHysteresis(bool wasPressed, unsigned position) {
    return wasPressed ? position > kPeriodicPressOff : position >= kPeriodicPressOn;
}

void recordLatency(std::atomic_uint64_t& samples, std::atomic_uint64_t& total, std::atomic_uint64_t& maximum,
                   std::chrono::steady_clock::duration elapsed) {
    const auto microseconds = static_cast<std::uint64_t>(
        (std::max)(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count(),
                   static_cast<std::chrono::microseconds::rep>(0)));
    samples.fetch_add(1, std::memory_order_relaxed);
    total.fetch_add(microseconds, std::memory_order_relaxed);
    auto seen = maximum.load(std::memory_order_relaxed);
    while (microseconds > seen &&
           !maximum.compare_exchange_weak(seen, microseconds, std::memory_order_relaxed)) {
    }
}

bool meaningfullyDifferent(const ForceTriggerCommand& a, const ForceTriggerCommand& b) {
    if (a.mode != b.mode || a.side != b.side) return true;
    if (a.mode != TriggerMode::RecoilRattle) return a != b;
    for (std::size_t index = 0; index < a.params.size(); ++index) {
        if (index == 2) continue;
        if (a.params[index] != b.params[index]) return true;
    }
    const auto difference = a.params[2] > b.params[2] ? a.params[2] - b.params[2] : b.params[2] - a.params[2];
    return static_cast<unsigned>(difference) >= kPeriodicAmplitudeStep;
}

} // namespace

AdaptiveTriggerBridge::AdaptiveTriggerBridge(flydigi::Apex5Device& device, unsigned strengthPercent,
                                             bool zoneEmulation)
    : device_(device), strengthPercent_(strengthPercent), zoneEmulation_(zoneEmulation) {}

AdaptiveTriggerBridge::~AdaptiveTriggerBridge() {
    quiesce();
}

void AdaptiveTriggerBridge::quiesce() {
    quiesced_.store(true, std::memory_order_relaxed);
    std::thread worker;
    {
        std::lock_guard lock(periodicMutex_);
        periodicStop_ = true;
        worker = std::move(periodicThread_);
    }
    periodicSignal_.notify_all();
    if (worker.joinable()) worker.join();
}

void AdaptiveTriggerBridge::wakePeriodic() {
    {
        std::lock_guard lock(periodicMutex_);
        if (!periodicThread_.joinable() && !periodicStop_) {
            periodicThread_ = std::thread([this] { periodicLoop(); });
        }
    }
    periodicSignal_.notify_all();
}

void AdaptiveTriggerBridge::periodicLoop() {
    DeadlineTimer timer;
    auto next = std::chrono::steady_clock::now();
    while (true) {
        {
            std::unique_lock lock(periodicMutex_);
            if (periodicStop_) break;
            if (!periodicArmed_.load(std::memory_order_relaxed)) {
                periodicSignal_.wait(lock, [this] {
                    return periodicStop_ || periodicArmed_.load(std::memory_order_relaxed);
                });
                next = std::chrono::steady_clock::now();
                continue;
            }
        }
        next += kPeriodicTick;
        const auto now = std::chrono::steady_clock::now();
        if (next < now - std::chrono::milliseconds(50)) next = now;
        timer.waitUntil(next);
        const auto woke = std::chrono::steady_clock::now();
        recordLatency(periodicLatenessSamples_, periodicLatenessTotalUs_, periodicLatenessMaxUs_, woke - next);
        stepPeriodic(woke);
    }
}

void AdaptiveTriggerBridge::stepPeriodic(std::chrono::steady_clock::time_point now) {
    if (quiesced_.load(std::memory_order_relaxed) || failed_.load(std::memory_order_relaxed)) return;
    for (const auto side : {TriggerSide::Left, TriggerSide::Right}) {
        const auto index = side == TriggerSide::Left ? 0U : 1U;
        std::lock_guard sendLock(sendMutex_);
        std::optional<ForceTriggerCommand> command;
        {
            std::lock_guard lock(stateMutex_);
            auto& armed = armedPeriodic_[index];
            if (!armed) continue;
            const unsigned position = lastPosition_[index].load(std::memory_order_relaxed);
            const auto zone = armed->tracker.update(
                static_cast<std::uint8_t>(position), kVibrationZoneHysteresis);
            armed->pressed = pressedWithHysteresis(armed->pressed, position);
            const bool inRange = armed->pressed && zone >= armed->emulation.firstZone &&
                                 zone <= armed->emulation.lastZone;
            const double seconds = std::chrono::duration<double>(now - armed->epoch).count();
            const auto rendered = renderPeriodicCommand(
                side, armed->emulation, seconds, inRange, periodicUnitsForPosition(zoneUnits_, position));
            if (meaningfullyDifferent(rendered, armed->sent) &&
                now - armed->lastSendAt >= kPeriodicMinimumSpacing) {
                armed->sent = rendered;
                armed->lastSendAt = now;
                command = rendered;
                periodicTransitions_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (command && !send(scaleTriggerStrength(*command, strengthPercent_), 0)) return;
    }
}

void AdaptiveTriggerBridge::handle(const DualSenseFeedback& feedback) {
    if (failed_.load(std::memory_order_relaxed) || feedback.kind != FeedbackKind::HidOutput) return;

    constexpr std::uint8_t kRightTrigger = 0x04;
    constexpr std::uint8_t kLeftTrigger = 0x08;
    if ((feedback.enableBits1 & kRightTrigger) != 0) {
        apply(TriggerSide::Right, feedback.enableBits1, feedback.rightTriggerEffect);
    }
    if ((feedback.enableBits1 & kLeftTrigger) != 0) {
        apply(TriggerSide::Left, feedback.enableBits1, feedback.leftTriggerEffect);
    }
}

void AdaptiveTriggerBridge::recordBlock(TriggerSide side, std::uint8_t enableBits,
                                        const std::array<std::uint8_t, 11>& effect) {
    constexpr std::size_t kMaximumDistinctBlocks = 32;
    std::array<std::uint8_t, 12> block{};
    block[0] = enableBits;
    std::copy(effect.begin(), effect.end(), block.begin() + 1);
    const auto index = side == TriggerSide::Left ? 0U : 1U;

    std::string line;
    std::size_t number = 0;
    {
        std::lock_guard lock(stateMutex_);
        if (block == lastBlock_[index]) return;
        lastBlock_[index] = block;
        auto& seen = seenBlocks_[index];
        if (seen.size() >= kMaximumDistinctBlocks ||
            std::find(seen.begin(), seen.end(), block) != seen.end()) return;
        seen.push_back(block);
        number = seen.size();
        char text[8];
        for (std::size_t i = 0; i < block.size(); ++i) {
            std::snprintf(text, sizeof text, "%02X", block[i]);
            if (i == 1) line += " fx=";
            else if (i > 1) line += ' ';
            else line += "en=";
            line += text;
        }
        blockLog_[index].push_back(line);
    }
    std::cerr << "[triggers] " << (index == 0 ? "lt" : "rt") << " block #" << number
              << ' ' << line << '\n';
}

void AdaptiveTriggerBridge::apply(TriggerSide side, std::uint8_t enableBits,
                                  const std::array<std::uint8_t, 11>& effect) {
    recordBlock(side, enableBits, effect);

    // Like a DualSense, keep the current effect: games such as Call of Duty interleave
    // empty blocks with active effects, and releasing on them makes the trigger oscillate.
    if (effect[0] == 0) {
        neutral_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    auto translated = translateAdaptiveTrigger(side, effect, 0);
    if (!translated) {
        unsupported_.fetch_add(1, std::memory_order_relaxed);
        (side == TriggerSide::Left ? lastUnsupportedLeftType_ : lastUnsupportedRightType_)
            .store(effect[0], std::memory_order_relaxed);
        std::lock_guard lock(stateMutex_);
        ++unsupportedByType_[effect[0]];
        return;
    }
    (side == TriggerSide::Left ? lastLeftType_ : lastRightType_)
        .store(effect[0], std::memory_order_relaxed);
    const auto& identity = device_.identity();
    std::optional<BowEmulation> bow;
    std::optional<ZoneEmulation> zones;
    std::optional<PeriodicEmulation> periodic;
    if (identity && !identity->isApex4()) {
        bow = describeBowEmulation(side, effect);
        if (zoneEmulation_) {
            zones = describeZoneEmulation(effect);
            if (!quiesced_.load(std::memory_order_relaxed)) periodic = describePeriodicEmulation(effect);
        }
    }
    std::optional<ForceTriggerCommand> zoneCommand;
    std::optional<ForceTriggerCommand> periodicCommand;
    std::lock_guard sendLock(sendMutex_);
    {
        std::lock_guard lock(stateMutex_);
        const auto index = side == TriggerSide::Left ? 0U : 1U;
        auto& armed = armedBow_[index];
        auto& armedZone = armedZones_[index];
        auto& armedPeriodic = armedPeriodic_[index];
        if ((bow && armed && armed->effect == effect) ||
            (zones && armedZone && armedZone->effect == effect) ||
            (periodic && armedPeriodic && armedPeriodic->effect == effect)) {
            deduplicated_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (periodic) {
            const auto now = std::chrono::steady_clock::now();
            const auto tracker = armedPeriodic
                ? armedPeriodic->tracker
                : ZoneTracker(lastPosition_[index].load(std::memory_order_relaxed));
            const bool pressed = pressedWithHysteresis(
                armedPeriodic && armedPeriodic->pressed, lastPosition_[index].load(std::memory_order_relaxed));
            const bool inRange = pressed && tracker.zone() >= periodic->firstZone &&
                                 tracker.zone() <= periodic->lastZone;
            periodicCommand = renderPeriodicCommand(side, *periodic, 0.0, inRange, zoneUnits_);
            armedPeriodic = ArmedPeriodic{effect, *periodic, tracker, now, now, *periodicCommand, pressed};
            periodicProfiles_.fetch_add(1, std::memory_order_relaxed);
        } else {
            armedPeriodic.reset();
        }
        periodicArmed_.store(armedPeriodic_[0] || armedPeriodic_[1], std::memory_order_relaxed);
        if (bow) armed = ArmedBow{effect, *bow, false};
        else armed.reset();
        if (zones) {
            const auto tracker = armedZone
                ? armedZone->tracker
                : ZoneTracker(lastPosition_[index].load(std::memory_order_relaxed));
            zoneCommand = renderZoneCommand(side, *zones, tracker.zone(), zoneUnits_);
            armedZone = ArmedZones{effect, *zones, tracker, *zoneCommand};
            zoneProfiles_.fetch_add(1, std::memory_order_relaxed);
        } else {
            armedZone.reset();
        }
        emulationArmed_.store(armedBow_[0] || armedBow_[1] || armedZones_[0] || armedZones_[1],
                              std::memory_order_relaxed);
    }
    if (periodic) wakePeriodic();
    if (bow) *translated = bow->draw;
    if (zoneCommand) *translated = *zoneCommand;
    if (periodicCommand) *translated = *periodicCommand;
    *translated = scaleTriggerStrength(*translated, strengthPercent_);
    {
        std::lock_guard lock(stateMutex_);
        auto& previous = side == TriggerSide::Left ? lastLeft_ : lastRight_;
        if (previous == translated) {
            deduplicated_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    if (!send(*translated, effect[0])) return;
    translated_.fetch_add(1, std::memory_order_relaxed);
    if (translated->mode == TriggerMode::Normal) {
        normal_.fetch_add(1, std::memory_order_relaxed);
    } else {
        active_.fetch_add(1, std::memory_order_relaxed);
    }
}

bool AdaptiveTriggerBridge::send(const ForceTriggerCommand& command, std::uint8_t dualSenseType) {
    std::string writeError;
    if (!device_.queueTriggerRaw(command, writeError)) {
        writeFailures_.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard lock(errorMutex_);
            error_ = std::move(writeError);
        }
        failed_.store(true, std::memory_order_relaxed);
        return false;
    }
    const bool left = command.side == TriggerSide::Left;
    std::lock_guard lock(stateMutex_);
    (left ? lastLeft_ : lastRight_) = command;
    if (command.mode != TriggerMode::Normal) {
        if (dualSenseType != 0) (left ? lastActiveLeftType_ : lastActiveRightType_) = dualSenseType;
        (left ? lastActiveLeft_ : lastActiveRight_) = command;
    }
    return true;
}

void AdaptiveTriggerBridge::updateTriggerPositions(std::uint8_t left, std::uint8_t right) {
    lastPosition_[0].store(left, std::memory_order_relaxed);
    lastPosition_[1].store(right, std::memory_order_relaxed);
    if (!emulationArmed_.load(std::memory_order_relaxed) || failed_.load(std::memory_order_relaxed)) return;
    const auto entered = std::chrono::steady_clock::now();
    for (const auto side : {TriggerSide::Left, TriggerSide::Right}) {
        const auto position = side == TriggerSide::Left ? left : right;
        std::optional<ForceTriggerCommand> command;
        std::lock_guard sendLock(sendMutex_);
        {
            std::lock_guard lock(stateMutex_);
            const auto index = side == TriggerSide::Left ? 0U : 1U;
            auto& armed = armedBow_[index];
            if (armed) {
                if (!armed->ruptured && position >= armed->emulation.ruptureAxis) {
                    command = armed->emulation.rupture;
                    armed->ruptured = true;
                } else if (armed->ruptured && position <= armed->emulation.rearmAxis) {
                    command = armed->emulation.draw;
                    armed->ruptured = false;
                }
            }
            auto& armedZone = armedZones_[index];
            if (armedZone) {
                const auto hysteresis = armedZone->emulation.frequency != 0
                    ? kVibrationZoneHysteresis : kZoneHysteresis;
                const auto rendered = renderZoneCommand(
                    side, armedZone->emulation, armedZone->tracker.update(position, hysteresis), zoneUnits_);
                if (rendered != armedZone->sent) {
                    armedZone->sent = rendered;
                    command = rendered;
                    zoneTransitions_.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        if (command) {
            const bool sent = send(scaleTriggerStrength(*command, strengthPercent_), 0);
            recordLatency(zoneLatencySamples_, zoneLatencyTotalUs_, zoneLatencyMaxUs_,
                          std::chrono::steady_clock::now() - entered);
            if (!sent) return;
        }
    }
}

void AdaptiveTriggerBridge::setZoneUnits(ZoneUnits units) {
    std::lock_guard sendLock(sendMutex_);
    std::lock_guard lock(stateMutex_);
    zoneUnits_ = units;
}

bool AdaptiveTriggerBridge::failed() const noexcept {
    return failed_.load(std::memory_order_relaxed);
}

std::string AdaptiveTriggerBridge::error() const {
    std::lock_guard lock(errorMutex_);
    return error_;
}

AdaptiveTriggerBridgeStats AdaptiveTriggerBridge::stats() const {
    std::lock_guard lock(stateMutex_);
    std::vector<std::pair<std::uint8_t, std::uint64_t>> byType;
    for (unsigned type = 0; type < unsupportedByType_.size(); ++type) {
        if (unsupportedByType_[type] != 0) {
            byType.emplace_back(static_cast<std::uint8_t>(type), unsupportedByType_[type]);
        }
    }
    std::sort(byType.begin(), byType.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (byType.size() > 4) byType.resize(4);
    return {translated_.load(std::memory_order_relaxed),
            active_.load(std::memory_order_relaxed),
            normal_.load(std::memory_order_relaxed),
            deduplicated_.load(std::memory_order_relaxed),
            neutral_.load(std::memory_order_relaxed),
            unsupported_.load(std::memory_order_relaxed),
            writeFailures_.load(std::memory_order_relaxed),
            lastLeftType_.load(std::memory_order_relaxed),
            lastRightType_.load(std::memory_order_relaxed),
            lastLeft_, lastRight_,
            lastActiveLeftType_, lastActiveRightType_,
            lastActiveLeft_, lastActiveRight_,
            lastUnsupportedLeftType_.load(std::memory_order_relaxed),
            lastUnsupportedRightType_.load(std::memory_order_relaxed),
            std::move(byType), blockLog_[0], blockLog_[1],
            zoneProfiles_.load(std::memory_order_relaxed),
            zoneTransitions_.load(std::memory_order_relaxed),
            periodicProfiles_.load(std::memory_order_relaxed),
            periodicTransitions_.load(std::memory_order_relaxed),
            zoneLatencySamples_.load(std::memory_order_relaxed),
            zoneLatencyTotalUs_.load(std::memory_order_relaxed),
            zoneLatencyMaxUs_.load(std::memory_order_relaxed),
            periodicLatenessSamples_.load(std::memory_order_relaxed),
            periodicLatenessTotalUs_.load(std::memory_order_relaxed),
            periodicLatenessMaxUs_.load(std::memory_order_relaxed)};
}

} // namespace asb::dualsense
