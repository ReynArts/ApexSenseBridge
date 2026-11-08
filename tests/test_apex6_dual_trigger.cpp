#ifdef NDEBUG
#undef NDEBUG
#endif

#include "dualsense/Apex6HapticBridge.h"
#include "dualsense/Apex6TriggerEffect.h"
#include "flydigi/Apex5Device.h"
#include "flydigi/Apex6Protocol.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using asb::flydigi::Apex5Device;
using asb::flydigi::TransportPtr;
using asb::dualsense::Apex6DualPolicy;
using Route = asb::flydigi::apex6::TriggerRoute;
using Firmware = asb::flydigi::apex6::TriggerFirmwareEffect;

struct WriteRecord {
    std::int64_t startNs = 0;
    std::int64_t endNs = 0;
    std::uint8_t command = 0;
    std::array<std::uint8_t, 7> payload{};
    std::uint8_t triggerPeak = 0;
    std::array<std::int8_t, 8> trigger{};
    bool spike = false;

    std::uint8_t channel() const { return payload[0]; }
};

struct WriteProfile {
    std::uint32_t averageUs = 0;
    std::uint32_t jitterUs = 0;
    std::uint32_t spikeEvery = 0;
    std::uint32_t spikeUs = 0;
    std::uint32_t longSpikeEvery = 0;
    std::uint32_t longSpikeUs = 0;
    std::uint32_t failAt = 0;
    std::uint8_t failCommand = 0;
};

class TimedApex6Transport final : public asb::platform::HidTransport {
public:
    explicit TimedApex6Transport(WriteProfile profile) : profile_(profile) {
        info_.vendorId = asb::flydigi::apex6::kVendorId;
        info_.productId = asb::flydigi::apex6::kProductId;
        info_.usagePage = asb::flydigi::apex6::kUsagePage;
        info_.inputReportLength = 33;
        info_.outputReportLength = 33;
        records_.reserve(1U << 16U);
    }

    bool isOpen() const noexcept override { return true; }
    const asb::HidDeviceInfo& info() const noexcept override { return info_; }

    void arm() {
        std::lock_guard lock(mutex_);
        armed_ = true;
        origin_ = Clock::now();
    }

    bool writeOutputReport(std::span<const std::uint8_t> report, std::string& error) override {
        const auto startedAt = Clock::now();
        WriteRecord record{};
        record.command = report.size() > 3 ? report[3] : 0;
        for (std::size_t index = 0; index < record.payload.size() && 5 + index < report.size(); ++index) {
            record.payload[index] = report[5 + index];
        }
        if (record.command == asb::flydigi::apex6::kCmdRealtimeMotor && report.size() >= 30) {
            for (std::size_t index = 0; index < 8; ++index) {
                const int value = static_cast<int>(report[6 + index * 3]) - 128;
                record.trigger[index] = static_cast<std::int8_t>(value);
                record.triggerPeak = static_cast<std::uint8_t>(
                    (std::max)(static_cast<int>(record.triggerPeak), std::abs(value)));
            }
        }
        bool armed = false;
        std::uint32_t costUs = 0;
        bool fail = false;
        {
            std::lock_guard lock(mutex_);
            armed = armed_;
            if (armed && record.command != asb::flydigi::apex6::kCmdGetInfo) {
                ++armedWrites_;
                costUs = nextCost(record.spike);
                if (profile_.failAt != 0 && record.command == profile_.failCommand &&
                    ++failCandidates_ == profile_.failAt) {
                    fail = true;
                }
            }
        }
        if (costUs != 0) {
            const auto until = startedAt + std::chrono::microseconds(costUs);
            while (Clock::now() < until) {
            }
        }
        const auto endedAt = Clock::now();
        {
            std::lock_guard lock(mutex_);
            record.startNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                startedAt - origin_).count();
            record.endNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                endedAt - origin_).count();
            if (armed) records_.push_back(record);
            if (record.command == asb::flydigi::apex6::kCmdGetInfo) queueInfoReply();
        }
        if (fail) {
            error = "simulated write failure";
            return false;
        }
        return true;
    }

    asb::platform::HidReadStatus readInputReport(std::span<std::uint8_t> report,
                                                 std::chrono::milliseconds,
                                                 std::size_t& bytesRead,
                                                 std::string&) override {
        std::lock_guard lock(mutex_);
        bytesRead = 0;
        if (replies_.empty()) return asb::platform::HidReadStatus::Timeout;
        const auto reply = std::move(replies_.front());
        replies_.pop_front();
        std::copy(reply.begin(), reply.end(), report.begin());
        bytesRead = reply.size();
        return asb::platform::HidReadStatus::Data;
    }

    std::vector<WriteRecord> records() const {
        std::lock_guard lock(mutex_);
        return records_;
    }

    std::int64_t nowNs() const {
        std::lock_guard lock(mutex_);
        return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - origin_).count();
    }

private:
    std::uint32_t nextCost(bool& spike) {
        spike = false;
        if (profile_.longSpikeEvery != 0 && armedWrites_ % profile_.longSpikeEvery == 0) {
            spike = true;
            return profile_.longSpikeUs;
        }
        if (profile_.spikeEvery != 0 && armedWrites_ % profile_.spikeEvery == 0) {
            spike = true;
            return profile_.spikeUs;
        }
        if (profile_.averageUs == 0) return 0;
        random_ = random_ * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto span = 2U * profile_.jitterUs + 1U;
        const auto offset = static_cast<std::uint32_t>((random_ >> 33U) % span);
        return profile_.averageUs - profile_.jitterUs + offset;
    }

    void queueInfoReply() {
        std::vector<std::uint8_t> reply(33, 0);
        reply[1] = asb::flydigi::apex6::kMagic0;
        reply[2] = asb::flydigi::apex6::kMagic1;
        reply[3] = asb::flydigi::apex6::kCmdGetInfo;
        reply[4] = 1;
        reply[6] = asb::flydigi::apex6::kDeviceType;
        reply[30] = 0x9F;
        reply[32] = asb::flydigi::apex6::checksum(std::span<const std::uint8_t>(reply).subspan(1));
        replies_.push_back(std::move(reply));
    }

    WriteProfile profile_;
    asb::HidDeviceInfo info_{};
    mutable std::mutex mutex_;
    bool armed_ = false;
    Clock::time_point origin_ = Clock::now();
    std::uint64_t armedWrites_ = 0;
    std::uint64_t failCandidates_ = 0;
    std::uint64_t random_ = 0x9E3779B97F4A7C15ULL;
    std::deque<std::vector<std::uint8_t>> replies_;
    std::vector<WriteRecord> records_;
};

using Effect = std::array<std::uint8_t, 11>;
constexpr Effect kVibration90{0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 90, 0};
constexpr Effect kVibration60{0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 60, 0};
constexpr Effect kVibration120{0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 120, 0};
constexpr Effect kFeedbackFull{0x21, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 0, 0};
constexpr Effect kWeapon{0x25, 0x84, 0x00, 7, 0, 0, 0, 0, 0, 0, 0};
constexpr Effect kOff{0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr unsigned kBurstFramesForTest =
    asb::dualsense::apex6ImpactFrames(asb::dualsense::Apex6ImpactShape{});

enum class Scenario {
    SingleSide,
    BothIdentical,
    BothDifferent,
    DifferentWithBursts,
    FirmwareChurn,
};

const char* scenarioName(Scenario scenario) {
    switch (scenario) {
    case Scenario::SingleSide: return "single_side";
    case Scenario::BothIdentical: return "both_identical";
    case Scenario::BothDifferent: return "both_different";
    case Scenario::DifferentWithBursts: return "diff_bursts";
    case Scenario::FirmwareChurn: return "churn";
    }
    return "?";
}

struct RunResult {
    asb::dualsense::Apex6HapticBridgeStats stats;
    std::vector<WriteRecord> records;
    std::int64_t steadyFromNs = 0;
    std::int64_t steadyToNs = 0;
    double seconds = 0.0;
    std::string error;
    bool failed = false;
};

RunResult runScenario(Scenario scenario, unsigned rate, std::chrono::milliseconds duration,
                      const WriteProfile& profile,
                      std::optional<Apex6DualPolicy> policy = std::nullopt) {
    using namespace asb::dualsense;
    auto* transport = new TimedApex6Transport(profile);
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, false, 100, 100, 100, 0, 100, rate);
    if (policy) bridge.setDualPolicy(*policy);
    DualSenseFeedback feedback{};
    feedback.enableBits1 = 0x0C;
    std::uint8_t leftPosition = 255;
    std::uint8_t rightPosition = 255;
    switch (scenario) {
    case Scenario::SingleSide:
        feedback.leftTriggerEffect = kOff;
        feedback.rightTriggerEffect = kVibration90;
        break;
    case Scenario::BothIdentical:
        feedback.leftTriggerEffect = kVibration90;
        feedback.rightTriggerEffect = kVibration90;
        break;
    case Scenario::BothDifferent:
        feedback.leftTriggerEffect = kFeedbackFull;
        feedback.rightTriggerEffect = kVibration90;
        break;
    case Scenario::DifferentWithBursts:
        feedback.leftTriggerEffect = kFeedbackFull;
        feedback.rightTriggerEffect = kWeapon;
        rightPosition = 0;
        break;
    case Scenario::FirmwareChurn:
        feedback.leftTriggerEffect = kFeedbackFull;
        feedback.rightTriggerEffect = kVibration60;
        break;
    }
    bridge.updateTriggerPositions(leftPosition, rightPosition);
    bridge.handle(feedback);
    transport->arm();
    assert(bridge.start(error));
    const auto startedNs = transport->nowNs();
    const auto startedAt = Clock::now();
    const auto until = startedAt + duration;
    bool pressed = false;
    auto nextToggle = startedAt + std::chrono::milliseconds(100);
    auto nextChange = startedAt + std::chrono::milliseconds(50);
    unsigned changes = 0;
    while (Clock::now() < until && !bridge.failed()) {
        const auto now = Clock::now();
        if (scenario == Scenario::DifferentWithBursts && now >= nextToggle) {
            pressed = !pressed;
            bridge.updateTriggerPositions(leftPosition, pressed ? 255 : 0);
            nextToggle += std::chrono::milliseconds(pressed ? 180 : 120);
        }
        if (scenario == Scenario::FirmwareChurn && now >= nextChange) {
            ++changes;
            const auto phase = changes % 60;
            feedback.rightTriggerEffect = phase >= 50 ? kOff : (changes % 2 == 0 ? kVibration60 : kVibration120);
            bridge.handle(feedback);
            nextChange += std::chrono::milliseconds(50);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    RunResult result;
    result.steadyFromNs = startedNs + 100'000'000;
    result.steadyToNs = transport->nowNs();
    result.seconds = std::chrono::duration<double>(Clock::now() - startedAt).count();
    result.failed = bridge.failed();
    result.error = bridge.error();
    std::string stopError;
    (void)bridge.stop(stopError);
    result.stats = bridge.stats();
    result.records = transport->records();
    return result;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(
        std::clamp(fraction * static_cast<double>(values.size() - 1), 0.0,
                   static_cast<double>(values.size() - 1)));
    return values[index];
}

struct TimingSummary {
    double writesPerSecond = 0.0;
    double framesPerSecond = 0.0;
    double routeWritesPerSecond = 0.0;
    double intervalMeanMs = 0.0;
    double intervalP99Ms = 0.0;
    double intervalMaxMs = 0.0;
    unsigned lateFrames = 0;
    unsigned spikes = 0;
    double busyP99Ms = 0.0;
    double busyMaxMs = 0.0;
    double headMaxMs = 0.0;
    double headMeanMs = 0.0;
    double secondHalfLateMaxMs = 0.0;
};

TimingSummary summarize(const RunResult& run, unsigned rate) {
    TimingSummary summary;
    std::vector<WriteRecord> steady;
    for (const auto& record : run.records) {
        if (record.startNs >= run.steadyFromNs && record.startNs < run.steadyToNs) {
            steady.push_back(record);
        }
    }
    const double seconds = static_cast<double>(run.steadyToNs - run.steadyFromNs) / 1e9;
    if (steady.empty() || seconds <= 0.0) return summary;
    std::vector<std::size_t> periodFirst;
    std::vector<std::size_t> secondHalf;
    bool expectFirst = true;
    for (std::size_t index = 0; index < steady.size(); ++index) {
        const auto& record = steady[index];
        if (record.spike) ++summary.spikes;
        if (record.command == asb::flydigi::apex6::kCmdMotorRoute) summary.routeWritesPerSecond += 1.0;
        if (record.command != asb::flydigi::apex6::kCmdRealtimeMotor) continue;
        summary.framesPerSecond += 1.0;
        if (rate != 2000 || expectFirst) periodFirst.push_back(index);
        else secondHalf.push_back(index);
        if (rate == 2000) expectFirst = !expectFirst;
    }
    summary.writesPerSecond = static_cast<double>(steady.size()) / seconds;
    summary.framesPerSecond /= seconds;
    summary.routeWritesPerSecond /= seconds;
    std::vector<double> intervals;
    std::vector<double> busy;
    std::vector<double> heads;
    const auto headOf = [&steady](std::size_t frame) {
        std::size_t first = frame;
        while (first > 0 && steady[first - 1].command != asb::flydigi::apex6::kCmdRealtimeMotor &&
               steady[first].startNs - steady[first - 1].endNs < 200'000) {
            --first;
        }
        return static_cast<double>(steady[frame].startNs - steady[first].startNs) / 1e6;
    };
    for (std::size_t period = 0; period + 1 < periodFirst.size(); ++period) {
        const auto from = periodFirst[period];
        const auto to = periodFirst[period + 1];
        const double interval = static_cast<double>(steady[to].startNs - steady[from].startNs) / 1e6;
        double busyMs = 0.0;
        bool spike = false;
        for (std::size_t index = from; index < to; ++index) {
            busyMs += static_cast<double>(steady[index].endNs - steady[index].startNs) / 1e6;
            spike = spike || steady[index].spike;
        }
        const double head = headOf(to);
        if (head > 0.0) heads.push_back(head);
        if (spike) continue;
        intervals.push_back(interval);
        busy.push_back(busyMs);
        if (interval > 9.0) ++summary.lateFrames;
    }
    for (const auto second : secondHalf) {
        if (second == 0) continue;
        const auto firstHalf = second - 1;
        if (steady[firstHalf].command != asb::flydigi::apex6::kCmdRealtimeMotor) continue;
        const double nominal = static_cast<double>(steady[firstHalf].startNs) / 1e6 -
                               headOf(firstHalf) + 4.0;
        const double late = static_cast<double>(steady[second].startNs) / 1e6 - nominal;
        if (!steady[firstHalf].spike) summary.secondHalfLateMaxMs = (std::max)(summary.secondHalfLateMaxMs, late);
    }
    if (!intervals.empty()) {
        double total = 0.0;
        for (const auto value : intervals) total += value;
        summary.intervalMeanMs = total / static_cast<double>(intervals.size());
        summary.intervalP99Ms = percentile(intervals, 0.99);
        summary.intervalMaxMs = *std::max_element(intervals.begin(), intervals.end());
        summary.busyP99Ms = percentile(busy, 0.99);
        summary.busyMaxMs = *std::max_element(busy.begin(), busy.end());
    }
    if (!heads.empty()) {
        double total = 0.0;
        for (const auto value : heads) total += value;
        summary.headMeanMs = total / static_cast<double>(heads.size());
        summary.headMaxMs = *std::max_element(heads.begin(), heads.end());
    }
    return summary;
}

void printHeader() {
    std::printf("%-15s %-5s %-10s %9s %9s %8s %7s %7s %8s %6s %6s %6s %6s %6s %5s %6s %6s %6s %6s %5s %7s\n",
                "scenario", "rate", "variant", "render_ns", "rndr_max", "writes/s", "0x57/s",
                "0x53/s", "retgt/s", "overr", "spikes", "int_mn", "int_99", "int_mx", "late",
                "busy99", "busymx", "head_m", "head_x", "h2lat", "fw_us/s");
}

void printRow(Scenario scenario, unsigned rate, const char* variant, const RunResult& run) {
    const auto summary = summarize(run, rate);
    const auto& stats = run.stats;
    const double renderMean = stats.renderedBlocks == 0 ? 0.0
        : static_cast<double>(stats.renderTotalNs) / static_cast<double>(stats.renderedBlocks);
    std::printf("%-15s %-5u %-10s %9.0f %9llu %8.1f %7.1f %7.1f %8.2f %6llu %6u %6.2f %6.2f %6.2f %5u %6.2f %6.2f %6.2f %6.2f %5.2f %7.0f\n",
                scenarioName(scenario), rate, variant, renderMean,
                static_cast<unsigned long long>(stats.renderMaximumNs),
                summary.writesPerSecond, summary.framesPerSecond, summary.routeWritesPerSecond,
                static_cast<double>(stats.triggerRetargets) / run.seconds,
                static_cast<unsigned long long>(stats.deadlineOverruns), summary.spikes,
                summary.intervalMeanMs, summary.intervalP99Ms, summary.intervalMaxMs,
                summary.lateFrames, summary.busyP99Ms, summary.busyMaxMs, summary.headMeanMs,
                summary.headMaxMs, summary.secondHalfLateMaxMs,
                static_cast<double>(stats.dualFirmwareWriteUs) / run.seconds);
    std::fflush(stdout);
}

WriteProfile measuredProfile() {
    WriteProfile profile;
    profile.averageUs = 1800;
    profile.jitterUs = 400;
    profile.spikeEvery = 997;
    profile.spikeUs = 30000;
    profile.longSpikeEvery = 4001;
    profile.longSpikeUs = 80000;
    return profile;
}

enum class SideMode : std::uint8_t { Off, Realtime, Firmware };

struct FirmwareReplay {
    std::array<SideMode, 2> mode{SideMode::Realtime, SideMode::Realtime};
    std::array<Firmware, 2> program{};
    unsigned triggerFrames = 0;
    unsigned misroutedFrames = 0;
    unsigned contentWhileFirmware = 0;
    unsigned bothFirmwareFrames = 0;
    unsigned routeWrites = 0;
    unsigned firmwareWrites = 0;
    unsigned persistentWrites = 0;
    unsigned singleFrames = 0;
    unsigned bothFrames = 0;
    std::vector<unsigned> runs;
    std::vector<std::int8_t> mixedSamples;
};

FirmwareReplay replayFirmware(const std::vector<WriteRecord>& records) {
    FirmwareReplay replay;
    unsigned lastRoute = 99;
    unsigned run = 0;
    for (const auto& record : records) {
        if (record.command == asb::flydigi::apex6::kCmdMotorRoute) {
            if (record.payload[0] != 0x01) ++replay.persistentWrites;
            const auto side = record.payload[1];
            if (side > 0x02) continue;
            ++replay.routeWrites;
            const auto modeByte = record.payload[2];
            const auto mode = modeByte == 0x03 ? SideMode::Realtime
                            : modeByte == 0x04 ? SideMode::Firmware : SideMode::Off;
            if (mode == SideMode::Firmware) {
                ++replay.firmwareWrites;
                assert(side <= 0x01);
            }
            for (std::size_t index = 0; index < 2; ++index) {
                if (side == index || side == 0x02) {
                    replay.mode[index] = mode;
                    if (mode == SideMode::Firmware) {
                        replay.program[index] = Firmware{record.payload[3], record.payload[4],
                                                         record.payload[5], record.payload[6]};
                    }
                }
            }
            continue;
        }
        if (record.command != asb::flydigi::apex6::kCmdRealtimeMotor) continue;
        const bool firmwareBoth = replay.mode[0] == SideMode::Firmware &&
                                  replay.mode[1] == SideMode::Firmware;
        if (firmwareBoth) ++replay.bothFirmwareFrames;
        const bool trigger = (record.channel() & 0x04U) != 0;
        if (!trigger || record.triggerPeak == 0) continue;
        ++replay.triggerFrames;
        if (replay.mode[0] == SideMode::Firmware || replay.mode[1] == SideMode::Firmware) {
            if (firmwareBoth) ++replay.contentWhileFirmware;
        }
        const unsigned route = record.channel() & 0x03U;
        const bool leftRt = replay.mode[0] == SideMode::Realtime;
        const bool rightRt = replay.mode[1] == SideMode::Realtime;
        if (route == 2U) {
            ++replay.bothFrames;
            if (!(leftRt && rightRt)) ++replay.misroutedFrames;
            replay.mixedSamples.insert(replay.mixedSamples.end(), record.trigger.begin(), record.trigger.end());
        } else {
            ++replay.singleFrames;
            const bool wantLeft = route == 0U;
            if (wantLeft ? (!leftRt || rightRt) : (!rightRt || leftRt)) ++replay.misroutedFrames;
        }
        if (route == lastRoute) {
            ++run;
        } else {
            if (run != 0) replay.runs.push_back(run);
            run = 1;
            lastRoute = route;
        }
    }
    if (run != 0) replay.runs.push_back(run);
    return replay;
}

asb::dualsense::Apex6TriggerEffect decoded(const Effect& bytes) {
    const auto effect = asb::dualsense::decodeApex6TriggerEffect(asb::TriggerSide::Right, bytes, 0);
    assert(effect);
    return *effect;
}

void testProtocolFramesMatchTheValidatedProbe() {
    using namespace asb::flydigi::apex6;
    assert(buildTriggerEnable(TriggerRoute::Left) == buildTriggerSideEnable(TriggerRoute::Left)[0]);
    assert(buildTriggerEnable(TriggerRoute::Both) == buildMotorEnable(true)[0]);
    assert(buildTriggerDisable(TriggerRoute::Both) == buildMotorDisable()[0]);
    const auto left = buildTriggerFirmwareEffect(TriggerRoute::Left, {0x00, 0x3C, 0x05, 0x14});
    const auto right = buildTriggerFirmwareEffect(TriggerRoute::Right, {0x01, 0xC8, 0x00, 0x32});
    assert(left && right);
    const std::array<std::uint8_t, 7> leftBytes{0x01, 0x00, 0x04, 0x00, 0x3C, 0x05, 0x14};
    const std::array<std::uint8_t, 7> rightBytes{0x01, 0x01, 0x04, 0x01, 0xC8, 0x00, 0x32};
    assert(std::equal(leftBytes.begin(), leftBytes.end(), left->begin() + 5));
    assert(std::equal(rightBytes.begin(), rightBytes.end(), right->begin() + 5));
    assert((*left)[3] == kCmdMotorRoute && (*left)[4] == 9);
    assert(isValidFrame(*left, kCmdMotorRoute) && isValidFrame(*right, kCmdMotorRoute));
    assert(!buildTriggerFirmwareEffect(TriggerRoute::Both, {}));
    assert(!buildTriggerFirmwareEffect(TriggerRoute::Mute, {}));
    assert(!buildTriggerFirmwareEffect(TriggerRoute::Left, {0x02, 1, 1, 1}));
}

void testFirmwareMappingTable() {
    using namespace asb::dualsense;
    using asb::flydigi::apex6::kTriggerFirmwareLoop;
    using asb::flydigi::apex6::kTriggerFirmwareOneShot;
    const auto vibration = apex6FirmwareEffectFor(decoded(kVibration60));
    assert(vibration && *vibration == (Firmware{kTriggerFirmwareLoop, 150, 60, 0}));
    const auto feedback = apex6FirmwareEffectFor(decoded(kFeedbackFull));
    assert(feedback && *feedback == (Firmware{kTriggerFirmwareLoop, 150, 85, 0}));
    const auto weapon = apex6FirmwareEffectFor(decoded(kWeapon));
    assert(weapon && *weapon == (Firmware{kTriggerFirmwareOneShot, 255, 0, 180}));
    const Effect weakVibration{0x26, 0xF0, 0x03, 0, 0, 0, 0, 0, 0, 40, 0};
    const auto weak = apex6FirmwareEffectFor(decoded(weakVibration));
    assert(weak && weak->strength == 19 && weak->parameter == 40 && weak->start == 103);
    const Effect bowBytes{0x22, 0x82, 0x00, 0x3F, 0, 0, 0, 0, 0, 0, 0};
    const auto bow = apex6FirmwareEffectFor(decoded(bowBytes));
    assert(bow && bow->type == kTriggerFirmwareOneShot && bow->strength == 255 && bow->start == 180);
    const std::array<std::pair<Apex6WeaponPreset, std::uint8_t>, 4> trains{{
        {Apex6WeaponPreset::MachineGun, std::uint8_t{4}},
        {Apex6WeaponPreset::Rifle, std::uint8_t{10}},
        {Apex6WeaponPreset::SubmachineGun, std::uint8_t{16}},
        {Apex6WeaponPreset::LightMachineGun, std::uint8_t{40}},
    }};
    for (const auto& [preset, parameter] : trains) {
        for (const unsigned level : {1U, 4U}) {
            const auto train = apex6FirmwareEffectFor(makeApex6WeaponPreset(preset, level));
            assert(train && train->type == kTriggerFirmwareLoop && train->parameter == parameter);
            assert(train->start == 80);
            assert(train->strength == (level == 4 ? 150 : 75));
        }
    }
    const auto sniper = apex6FirmwareEffectFor(makeApex6WeaponPreset(Apex6WeaponPreset::SniperRifle, 4));
    assert(sniper && sniper->type == kTriggerFirmwareOneShot && sniper->start == 178);
    const auto pistol = apex6FirmwareEffectFor(makeApex6WeaponPreset(Apex6WeaponPreset::Pistol, 4));
    assert(pistol && pistol->type == kTriggerFirmwareOneShot && pistol->start == 50);
    const Effect machine{0x27, 0xFE, 0x03, 0x3F, 0x55, 0x28, 0, 0, 0, 0, 0};
    if (const auto machineEffect = decodeApex6TriggerEffect(asb::TriggerSide::Right, machine, 0);
        machineEffect && machineEffect->type == Apex6TriggerType::Rhythm) {
        const auto mapped = apex6FirmwareEffectFor(*machineEffect);
        if (machineEffect->rhythm.periodSamples != 0 &&
            (machineEffect->rhythm.levelA > 0.0 || machineEffect->rhythm.levelB > 0.0)) {
            assert(mapped && mapped->type == kTriggerFirmwareLoop);
        }
    }
    Apex6TriggerEffect gallop{};
    gallop.type = Apex6TriggerType::Rhythm;
    gallop.rhythm.periodSamples = 400;
    gallop.rhythm.drive = 64;
    gallop.rhythm.activeFrom = 26;
    const auto gallopMapped = apex6FirmwareEffectFor(gallop);
    assert(gallopMapped && gallopMapped->parameter == 3 && gallopMapped->strength == 64 &&
           gallopMapped->start == 26);
    Apex6TriggerEffect legacy{};
    legacy.type = Apex6TriggerType::Legacy;
    legacy.legacy.mode = asb::TriggerMode::Vibration;
    legacy.legacy.params = {96, 0, 112, 40};
    const auto legacyMapped = apex6FirmwareEffectFor(legacy);
    assert(legacyMapped && *legacyMapped == (Firmware{kTriggerFirmwareLoop, 150, 40, 127}));
    legacy.legacy.mode = asb::TriggerMode::Normal;
    assert(!apex6FirmwareEffectFor(legacy));
    assert(!apex6FirmwareEffectFor(Apex6TriggerEffect{}));
    Apex6TriggerEffect silent{};
    silent.type = Apex6TriggerType::Rhythm;
    assert(!apex6FirmwareEffectFor(silent));
    Apex6TriggerEffect zeroImpact{};
    zeroImpact.type = Apex6TriggerType::Impact;
    assert(!apex6FirmwareEffectFor(zeroImpact));
}

struct ArbiterStep {
    bool left = false;
    bool right = false;
    bool leftBurst = false;
    bool rightBurst = false;
    bool leftNew = false;
    bool rightNew = false;
    bool identical = false;
};

asb::dualsense::Apex6DualDecision step(asb::dualsense::Apex6DualArbiter& arbiter, std::uint64_t& block,
                                       Apex6DualPolicy policy, const ArbiterStep& input,
                                       bool capable = true, bool leftOn = true, bool rightOn = true) {
    asb::dualsense::Apex6DualInput value;
    value.block = block++;
    value.leftActive = input.left;
    value.rightActive = input.right;
    value.leftBurst = input.leftBurst;
    value.rightBurst = input.rightBurst;
    value.leftNewEffect = input.leftNew;
    value.rightNewEffect = input.rightNew;
    value.identical = input.identical;
    value.leftEffectOn = leftOn;
    value.rightEffectOn = rightOn;
    value.firmwareCapable = capable;
    value.policy = policy;
    return arbiter.next(value);
}

void testArbiterKeepsTodaysSingleAndIdenticalRouting() {
    using namespace asb::dualsense;
    static_assert(kApex6DefaultDualPolicy == Apex6DualPolicy::Mix);
    for (const auto policy : {Apex6DualPolicy::Mix, Apex6DualPolicy::Priority, Apex6DualPolicy::Firmware}) {
        Apex6DualArbiter arbiter;
        std::uint64_t block = 0;
        for (unsigned index = 0; index < 100; ++index) {
            const auto decision = step(arbiter, block, policy, {false, true});
            assert(!decision.episode && !decision.firmware && decision.route == Route::Right);
        }
        for (unsigned index = 0; index < 10; ++index) {
            const auto decision = step(arbiter, block, policy, {false, false});
            assert(!decision.episode && decision.route == Route::Mute);
        }
        for (unsigned index = 0; index < 100; ++index) {
            const auto decision = step(arbiter, block, policy, {true, false});
            assert(!decision.episode && decision.route == Route::Left);
        }
        for (unsigned index = 0; index < 100; ++index) {
            const auto decision = step(arbiter, block, policy, {true, true, false, false, false, false, true});
            assert(!decision.episode && decision.route == Route::Both);
        }
    }
}

void testArbiterMixHoldsBothAcrossIntermittentShots() {
    using namespace asb::dualsense;
    Apex6DualArbiter arbiter;
    std::uint64_t block = 0;
    unsigned episodes = 0;
    unsigned singleBlocks = 0;
    for (unsigned index = 0; index < 1956; ++index) {
        const bool shot = index >= 20 && (index % 50) < 6;
        const auto decision = step(arbiter, block, Apex6DualPolicy::Mix, {true, shot, false, shot});
        if (decision.change == Apex6DualChange::Episode) ++episodes;
        if (index >= 50) {
            assert(decision.episode && decision.mix && decision.route == Route::Both);
        }
        if (decision.route != Route::Both) ++singleBlocks;
    }
    assert(episodes == 1);
    assert(singleBlocks == 50);
    for (unsigned index = 0; index < kApex6DualMixReleaseBlocks; ++index) {
        const auto decision = step(arbiter, block, Apex6DualPolicy::Mix, {true, false});
        assert(decision.episode && decision.route == Route::Both);
    }
    const auto decision = step(arbiter, block, Apex6DualPolicy::Mix, {true, false});
    assert(!decision.episode && decision.route == Route::Left);
}

void testArbiterPriorityBurstWinsAndReturns() {
    using namespace asb::dualsense;
    Apex6DualArbiter arbiter;
    std::uint64_t block = 0;
    for (unsigned index = 0; index < 20; ++index) {
        assert(step(arbiter, block, Apex6DualPolicy::Priority, {true, false}).route == Route::Left);
    }
    auto decision = step(arbiter, block, Apex6DualPolicy::Priority, {true, true, false, true});
    assert(decision.episode && decision.route == Route::Right);
    for (unsigned index = 0; index < 6; ++index) {
        decision = step(arbiter, block, Apex6DualPolicy::Priority, {true, true, false, true});
        assert(decision.route == Route::Right);
    }
    decision = step(arbiter, block, Apex6DualPolicy::Priority, {true, false});
    assert(decision.route == Route::Left && decision.change == Apex6DualChange::Return);
    for (unsigned index = 0; index < 4; ++index) {
        decision = step(arbiter, block, Apex6DualPolicy::Priority, {true, false});
        assert(decision.route == Route::Left);
    }
    decision = step(arbiter, block, Apex6DualPolicy::Priority, {true, true, false, true});
    assert(decision.route == Route::Right && decision.change == Apex6DualChange::Burst);
    decision = step(arbiter, block, Apex6DualPolicy::Priority, {true, true, true, true});
    assert(decision.route == Route::Right);
    decision = step(arbiter, block, Apex6DualPolicy::Priority, {true, false, true, false});
    assert(decision.route == Route::Left);
}

void testArbiterPriorityHysteresis() {
    using namespace asb::dualsense;
    Apex6DualArbiter arbiter;
    std::uint64_t block = 0;
    for (unsigned index = 0; index < 50; ++index) step(arbiter, block, Apex6DualPolicy::Priority, {true, false});
    auto decision = step(arbiter, block, Apex6DualPolicy::Priority, {true, true});
    assert(decision.episode && decision.route == Route::Right && decision.change == Apex6DualChange::Episode);
    unsigned changes = 0;
    Route previous = decision.route;
    for (unsigned index = 0; index < 1000; ++index) {
        const bool rightPulse = (index % 12) < 6;
        decision = step(arbiter, block, Apex6DualPolicy::Priority, {true, rightPulse});
        assert(decision.episode);
        if (decision.route != previous) ++changes;
        previous = decision.route;
    }
    assert(changes == 0);
    std::uint64_t lastChange = 0;
    changes = 0;
    previous = decision.route;
    for (unsigned index = 0; index < 1000; ++index) {
        const bool rightPulse = (index % 24) < 6;
        decision = step(arbiter, block, Apex6DualPolicy::Priority, {true, rightPulse});
        if (decision.route != previous) {
            ++changes;
            assert(block - lastChange >= kApex6DualOwnerHoldBlocks);
            lastChange = block;
        }
        previous = decision.route;
    }
    assert(changes <= 1000 / kApex6DualOwnerHoldBlocks + 1);
    decision = step(arbiter, block, Apex6DualPolicy::Priority, {true, true, false, false, true, false});
    assert(decision.route == Route::Left || decision.change == Apex6DualChange::None);
    Apex6DualArbiter fresh;
    block = 0;
    step(fresh, block, Apex6DualPolicy::Priority, {true, false});
    decision = step(fresh, block, Apex6DualPolicy::Priority, {true, true});
    assert(decision.route == Route::Right);
    decision = step(fresh, block, Apex6DualPolicy::Priority, {true, true, false, false, true, false});
    assert(decision.route == Route::Left && decision.change == Apex6DualChange::NewEffect);
}

void testArbiterFirmwareEntryAndExit() {
    using namespace asb::dualsense;
    Apex6DualArbiter arbiter;
    std::uint64_t block = 0;
    for (unsigned index = 0; index < 10; ++index) step(arbiter, block, Apex6DualPolicy::Firmware, {true, false});
    auto decision = step(arbiter, block, Apex6DualPolicy::Firmware, {true, true, false, true});
    assert(decision.episode && !decision.firmware && decision.route == Route::Right);
    for (unsigned index = 0; index < 5; ++index) {
        decision = step(arbiter, block, Apex6DualPolicy::Firmware, {true, true, false, true});
        assert(!decision.firmware);
    }
    decision = step(arbiter, block, Apex6DualPolicy::Firmware, {true, false});
    assert(decision.firmware);
    for (unsigned index = 0; index < 600; ++index) {
        const bool shot = (index % 125) < 6;
        decision = step(arbiter, block, Apex6DualPolicy::Firmware, {true, shot, false, shot});
        assert(decision.firmware);
    }
    decision = step(arbiter, block, Apex6DualPolicy::Firmware, {true, true});
    for (unsigned index = 0; index < kApex6DualFirmwareReleaseBlocks - 10; ++index) {
        decision = step(arbiter, block, Apex6DualPolicy::Firmware, {true, false});
    }
    assert(decision.firmware);
    for (unsigned index = 0; index < 20; ++index) decision = step(arbiter, block, Apex6DualPolicy::Firmware, {true, false});
    assert(!decision.firmware && !decision.episode && decision.route == Route::Left);

    Apex6DualArbiter offGrace;
    block = 0;
    decision = step(offGrace, block, Apex6DualPolicy::Firmware, {true, true});
    assert(decision.firmware);
    for (unsigned index = 0; index < kApex6DualFirmwareOffGraceBlocks; ++index) {
        decision = step(offGrace, block, Apex6DualPolicy::Firmware, {true, index % 4 == 0}, true, true, index % 4 == 0);
        assert(decision.firmware);
    }
    for (unsigned index = 0; index <= kApex6DualFirmwareOffGraceBlocks; ++index) {
        decision = step(offGrace, block, Apex6DualPolicy::Firmware, {true, false}, true, true, false);
    }
    assert(!decision.firmware);

    Apex6DualArbiter unmapped;
    block = 0;
    decision = step(unmapped, block, Apex6DualPolicy::Firmware, {true, true}, false);
    assert(decision.episode && !decision.firmware && decision.route == Route::Right);
}

void testArbiterCostIsNegligible() {
    using namespace asb::dualsense;
    Apex6DualArbiter arbiter;
    constexpr std::uint64_t kCalls = 2'000'000;
    unsigned sink = 0;
    std::uint64_t block = 0;
    const auto startedAt = Clock::now();
    for (std::uint64_t index = 0; index < kCalls; ++index) {
        const bool burst = (index % 97) < 7;
        const auto decision = step(arbiter, block, static_cast<Apex6DualPolicy>(index % 3),
                                   {(index % 13) != 0, true, false, burst});
        sink += static_cast<unsigned>(decision.route);
    }
    const double nanoseconds = std::chrono::duration<double, std::nano>(Clock::now() - startedAt).count() /
                               static_cast<double>(kCalls);
    std::array<std::int8_t, 16> left{};
    std::array<std::int8_t, 16> right{};
    std::array<std::int8_t, 16> mixed{};
    for (std::size_t index = 0; index < left.size(); ++index) {
        left[index] = static_cast<std::int8_t>(index * 7);
        right[index] = static_cast<std::int8_t>(120 - static_cast<int>(index));
    }
    const auto mixStartedAt = Clock::now();
    for (std::uint64_t index = 0; index < kCalls; ++index) {
        left[index % 16] = static_cast<std::int8_t>(index & 0x7F);
        for (std::size_t sample = 0; sample < mixed.size(); ++sample) {
            mixed[sample] = static_cast<std::int8_t>(std::clamp(
                static_cast<int>(left[sample]) + static_cast<int>(right[sample]),
                -asb::dualsense::kApex6DualMixPeak, asb::dualsense::kApex6DualMixPeak));
        }
        sink += static_cast<unsigned>(mixed[index % 16]);
    }
    const double mixNanoseconds = std::chrono::duration<double, std::nano>(Clock::now() - mixStartedAt).count() /
                                  static_cast<double>(kCalls);
    std::printf("dual_arbiter_ns_per_block=%.1f mix_sum_ns_per_16_samples=%.1f checksum=%u\n",
                nanoseconds, mixNanoseconds, sink);
    assert(nanoseconds < 1000.0 && mixNanoseconds < 1000.0);
}

void testBridgeSingleSideAndIdenticalPathsAreUnchanged() {
    for (const unsigned rate : {1000U, 2000U}) {
        for (const auto policy : {Apex6DualPolicy::Mix, Apex6DualPolicy::Priority, Apex6DualPolicy::Firmware}) {
            const auto single = runScenario(Scenario::SingleSide, rate, std::chrono::milliseconds(300),
                                            WriteProfile{}, policy);
            assert(!single.failed && single.stats.writeFailures == 0);
            assert(single.stats.dualTriggerBlocks == 0 && single.stats.triggerRetargets == 1);
            assert(single.stats.leftTriggerFrames == 0 && single.stats.dualFirmwareWrites == 0);
            assert(single.stats.dualMixFrames == 0);
            const auto singleReplay = replayFirmware(single.records);
            assert(singleReplay.misroutedFrames == 0 && singleReplay.routeWrites == 4);
            assert(singleReplay.runs.size() == 1 && singleReplay.singleFrames > 0);
            assert(singleReplay.persistentWrites == 0);

            const auto identical = runScenario(Scenario::BothIdentical, rate, std::chrono::milliseconds(300),
                                               WriteProfile{}, policy);
            assert(!identical.failed && identical.stats.writeFailures == 0);
            assert(identical.stats.dualTriggerBlocks == 0 && identical.stats.triggerRetargets == 0);
            assert(identical.stats.dualMixFrames == 0 && identical.stats.dualFirmwareWrites == 0);
            const auto identicalReplay = replayFirmware(identical.records);
            assert(identicalReplay.routeWrites == 2 && identicalReplay.singleFrames == 0);
            assert(identicalReplay.misroutedFrames == 0);
            if (rate == 2000) assert(single.stats.primeReports == 4 && identical.stats.primeReports == 4);
        }
    }
}

void testBridgeMixSumsBothEffectsOnTargetBoth() {
    using namespace asb::dualsense;
    for (const unsigned rate : {1000U, 2000U}) {
        auto* transport = new TimedApex6Transport(WriteProfile{});
        Apex5Device device{TransportPtr(transport)};
        std::string error;
        assert(device.verifyIdentity(error));
        Apex6HapticBridge bridge(device, {}, false, 100, 100, 100, 0, 100, rate);
        assert(bridge.dualPolicy() == Apex6DualPolicy::Mix);
        bridge.updateTriggerPositions(255, 255);
        DualSenseFeedback feedback{};
        feedback.enableBits1 = 0x0C;
        feedback.leftTriggerEffect = kFeedbackFull;
        feedback.rightTriggerEffect = kVibration90;
        bridge.handle(feedback);
        transport->arm();
        assert(bridge.start(error));
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        assert(bridge.stop(error));
        const auto stats = bridge.stats();
        assert(stats.writeFailures == 0 && stats.dualEpisodes == 1);
        assert(stats.dualMixFrames > 30 && stats.triggerRetargets == 0);
        assert(stats.leftTriggerFrames == 0 && stats.rightTriggerFrames == 0);
        assert(stats.dualMixClippedSamples > 0);
        const auto replay = replayFirmware(transport->records());
        assert(replay.misroutedFrames == 0 && replay.singleFrames == 0 && replay.bothFrames > 30);
        assert(replay.routeWrites == 2 && replay.firmwareWrites == 0);
        const auto peak = *std::max_element(replay.mixedSamples.begin(), replay.mixedSamples.end());
        assert(peak == kApex6DualMixPeak);
        assert(std::all_of(replay.mixedSamples.begin(), replay.mixedSamples.end(),
                           [](std::int8_t sample) { return sample >= -kApex6DualMixPeak; }));
    }
}

void testBridgeMixWeaponShotsDoNotBounceTarget() {
    for (const unsigned rate : {1000U, 2000U}) {
        const auto run = runScenario(Scenario::DifferentWithBursts, rate, std::chrono::milliseconds(1500),
                                     WriteProfile{}, Apex6DualPolicy::Mix);
        assert(!run.failed && run.stats.writeFailures == 0);
        assert(run.stats.weaponBreaks >= 3);
        const auto replay = replayFirmware(run.records);
        assert(replay.misroutedFrames == 0);
        assert(run.stats.dualEpisodes == 1);
        assert(run.stats.triggerRetargets <= 2);
        assert(replay.routeWrites <= 6);
    }
}

void testBridgePriorityBurstPreemptsWithoutClipping() {
    using namespace asb::dualsense;
    for (const unsigned rate : {1000U, 2000U}) {
        auto* transport = new TimedApex6Transport(WriteProfile{});
        Apex5Device device{TransportPtr(transport)};
        std::string error;
        assert(device.verifyIdentity(error));
        Apex6HapticBridge bridge(device, {}, false, 100, 100, 100, 0, 100, rate);
        bridge.setDualPolicy(Apex6DualPolicy::Priority);
        bridge.updateTriggerPositions(255, 0);
        DualSenseFeedback feedback{};
        feedback.enableBits1 = 0x0C;
        feedback.leftTriggerEffect = kVibration90;
        feedback.rightTriggerEffect = kWeapon;
        bridge.handle(feedback);
        transport->arm();
        assert(bridge.start(error));
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        bridge.updateTriggerPositions(255, 255);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        assert(bridge.stop(error));
        const auto stats = bridge.stats();
        assert(stats.weaponBreaks == 1 && stats.writeFailures == 0);
        assert(stats.dualBurstSwitches + stats.dualEpisodes >= 1 && stats.dualReturns == 1);
        assert(stats.dualMixFrames == 0 && stats.dualFirmwareWrites == 0);
        const auto replay = replayFirmware(transport->records());
        assert(replay.misroutedFrames == 0 && replay.bothFrames == 0);
        unsigned rightFrames = 0;
        unsigned rightRuns = 0;
        unsigned previous = 99;
        for (const auto& record : transport->records()) {
            if (record.command != asb::flydigi::apex6::kCmdRealtimeMotor || record.triggerPeak == 0) continue;
            const unsigned route = record.channel() & 0x03U;
            if (route == 1U) {
                ++rightFrames;
                if (previous != 1U) ++rightRuns;
            }
            previous = route;
        }
        const unsigned framesPerBlock = rate == 2000 ? 2U : 1U;
        assert(rightRuns == 1);
        assert(rightFrames >= (kBurstFramesForTest - 1) * framesPerBlock);
        assert(rightFrames <= kBurstFramesForTest * framesPerBlock);
        assert(previous == 0U);
    }
}

void testBridgePriorityContinuousPairNeverFlips() {
    for (const unsigned rate : {1000U, 2000U}) {
        const auto run = runScenario(Scenario::BothDifferent, rate, std::chrono::milliseconds(600),
                                     WriteProfile{}, Apex6DualPolicy::Priority);
        assert(!run.failed && run.stats.writeFailures == 0);
        assert(run.stats.dualEpisodes == 1 && run.stats.dualOwnerChanges == 0);
        const auto replay = replayFirmware(run.records);
        assert(replay.misroutedFrames == 0 && replay.bothFrames == 0);
        assert(replay.runs.size() == 1);
        assert(run.stats.triggerRetargets == 1);
    }
}

void testBridgeFirmwareBackendEntersAndRestores() {
    using namespace asb::dualsense;
    for (const unsigned rate : {1000U, 2000U}) {
        auto* transport = new TimedApex6Transport(WriteProfile{});
        Apex5Device device{TransportPtr(transport)};
        std::string error;
        assert(device.verifyIdentity(error));
        Apex6HapticBridge bridge(device, {}, false, 100, 100, 100, 0, 100, rate);
        bridge.setDualPolicy(Apex6DualPolicy::Firmware);
        bridge.updateTriggerPositions(255, 0);
        DualSenseFeedback feedback{};
        feedback.enableBits1 = 0x0C;
        feedback.leftTriggerEffect = kVibration60;
        feedback.rightTriggerEffect = kWeapon;
        bridge.handle(feedback);
        transport->arm();
        assert(bridge.start(error));
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        bridge.updateTriggerPositions(255, 255);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        auto stats = bridge.stats();
        assert(stats.dualFirmwareEntries == 1 && stats.dualFirmwareBlocks > 50);
        assert(stats.weaponBreaks == 1);
        auto replay = replayFirmware(transport->records());
        assert(replay.mode[0] == SideMode::Firmware && replay.mode[1] == SideMode::Firmware);
        assert(replay.program[0] == (Firmware{0x00, 150, 60, 0}));
        assert(replay.program[1] == (Firmware{0x01, 255, 0, 180}));
        assert(replay.contentWhileFirmware == 0 && replay.misroutedFrames == 0);
        assert(replay.firmwareWrites == 2 && replay.persistentWrites == 0);
        feedback.leftTriggerEffect = kVibration120;
        bridge.handle(feedback);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        stats = bridge.stats();
        assert(stats.dualFirmwareRefreshes == 1);
        replay = replayFirmware(transport->records());
        assert(replay.program[0].parameter == 120 && replay.firmwareWrites == 3);
        feedback.leftTriggerEffect = kOff;
        bridge.handle(feedback);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        stats = bridge.stats();
        assert(stats.dualFirmwareExits == 1 && stats.writeFailures == 0);
        replay = replayFirmware(transport->records());
        assert(replay.mode[0] != SideMode::Firmware && replay.mode[1] != SideMode::Firmware);
        assert(replay.contentWhileFirmware == 0 && replay.misroutedFrames == 0);
        assert(stats.dualFirmwareWrites == 5);
        assert(bridge.stop(error));
        replay = replayFirmware(transport->records());
        assert(replay.mode[0] == SideMode::Off && replay.mode[1] == SideMode::Off);
        assert(replay.persistentWrites == 0);
    }
}

void testBridgeFirmwareStopWhileEngagedClearsBothSides() {
    using namespace asb::dualsense;
    auto* transport = new TimedApex6Transport(WriteProfile{});
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, false, 100, 100, 100, 0, 100, 1000);
    bridge.setDualPolicy(Apex6DualPolicy::Firmware);
    bridge.updateTriggerPositions(255, 255);
    DualSenseFeedback feedback{};
    feedback.enableBits1 = 0x0C;
    feedback.leftTriggerEffect = kFeedbackFull;
    feedback.rightTriggerEffect = kVibration90;
    bridge.handle(feedback);
    transport->arm();
    assert(bridge.start(error));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    assert(bridge.stats().dualFirmwareEntries == 1);
    assert(bridge.stop(error));
    const auto replay = replayFirmware(transport->records());
    assert(replay.mode[0] == SideMode::Off && replay.mode[1] == SideMode::Off);
    bridge.setDualPolicy(Apex6DualPolicy::Mix);
    assert(bridge.dualPolicy() == Apex6DualPolicy::Mix);
}

void testBridgeFirmwareLivePolicySwitchRestores() {
    using namespace asb::dualsense;
    auto* transport = new TimedApex6Transport(WriteProfile{});
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, false, 100, 100, 100, 0, 100, 1000);
    bridge.setDualPolicy(Apex6DualPolicy::Firmware);
    bridge.updateTriggerPositions(255, 255);
    DualSenseFeedback feedback{};
    feedback.enableBits1 = 0x0C;
    feedback.leftTriggerEffect = kFeedbackFull;
    feedback.rightTriggerEffect = kVibration90;
    bridge.handle(feedback);
    transport->arm();
    assert(bridge.start(error));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    bridge.setDualPolicy(Apex6DualPolicy::Mix);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto stats = bridge.stats();
    assert(stats.dualFirmwareEntries == 1 && stats.dualFirmwareExits == 1 && stats.dualMixFrames > 5);
    const auto replay = replayFirmware(transport->records());
    assert(replay.mode[0] == SideMode::Realtime && replay.mode[1] == SideMode::Realtime);
    assert(replay.misroutedFrames == 0 && replay.contentWhileFirmware == 0);
    assert(bridge.stop(error));
}

void testBridgeRecordsRouteWriteFailures() {
    struct Case {
        Apex6DualPolicy policy;
        Scenario scenario;
        std::uint32_t failAt;
        bool firmwareWrite;
    };
    for (const auto& item : {Case{Apex6DualPolicy::Mix, Scenario::DifferentWithBursts, 3, false},
                             Case{Apex6DualPolicy::Mix, Scenario::DifferentWithBursts, 5, false},
                             Case{Apex6DualPolicy::Priority, Scenario::BothDifferent, 3, false},
                             Case{Apex6DualPolicy::Firmware, Scenario::BothDifferent, 3, false},
                             Case{Apex6DualPolicy::Firmware, Scenario::BothDifferent, 5, true},
                             Case{Apex6DualPolicy::Firmware, Scenario::BothDifferent, 6, true}}) {
        WriteProfile profile;
        profile.failCommand = asb::flydigi::apex6::kCmdMotorRoute;
        profile.failAt = item.failAt;
        const auto run = runScenario(item.scenario, 1000, std::chrono::milliseconds(400), profile, item.policy);
        assert(run.failed);
        assert(run.error == "simulated write failure");
        assert(run.stats.writeFailures == 1);
        assert((run.stats.dualFirmwareWrites != 0) == item.firmwareWrite);
    }
}

void runBenchmark(std::chrono::milliseconds duration) {
    printHeader();
    for (const unsigned rate : {1000U, 2000U}) {
        for (const auto scenario : {Scenario::SingleSide, Scenario::BothIdentical}) {
            const auto run = runScenario(scenario, rate, duration, measuredProfile());
            assert(!run.failed);
            printRow(scenario, rate, "default", run);
        }
        for (const auto scenario : {Scenario::BothDifferent, Scenario::DifferentWithBursts, Scenario::FirmwareChurn}) {
            for (const auto policy : {Apex6DualPolicy::Mix, Apex6DualPolicy::Priority, Apex6DualPolicy::Firmware}) {
                if (scenario == Scenario::FirmwareChurn && policy != Apex6DualPolicy::Firmware) continue;
                const auto run = runScenario(scenario, rate, duration, measuredProfile(), policy);
                assert(!run.failed);
                printRow(scenario, rate, std::string(asb::dualsense::apex6DualPolicyName(policy)).c_str(), run);
                if (policy == Apex6DualPolicy::Firmware) {
                    const auto& stats = run.stats;
                    std::printf("  firmware: entries=%llu exits=%llu refreshes=%llu writes=%llu write_us=%llu deferred=%llu us_per_write=%.0f\n",
                                static_cast<unsigned long long>(stats.dualFirmwareEntries),
                                static_cast<unsigned long long>(stats.dualFirmwareExits),
                                static_cast<unsigned long long>(stats.dualFirmwareRefreshes),
                                static_cast<unsigned long long>(stats.dualFirmwareWrites),
                                static_cast<unsigned long long>(stats.dualFirmwareWriteUs),
                                static_cast<unsigned long long>(stats.dualFirmwareDeferred),
                                stats.dualFirmwareWrites == 0 ? 0.0
                                    : static_cast<double>(stats.dualFirmwareWriteUs) /
                                          static_cast<double>(stats.dualFirmwareWrites));
                }
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    testProtocolFramesMatchTheValidatedProbe();
    testFirmwareMappingTable();
    testArbiterKeepsTodaysSingleAndIdenticalRouting();
    testArbiterMixHoldsBothAcrossIntermittentShots();
    testArbiterPriorityBurstWinsAndReturns();
    testArbiterPriorityHysteresis();
    testArbiterFirmwareEntryAndExit();
    testArbiterCostIsNegligible();
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == "--bench" && index + 1 < argc) {
            const auto seconds = std::strtoul(argv[index + 1], nullptr, 10);
            runBenchmark(std::chrono::milliseconds(seconds * 1000UL));
            return 0;
        }
    }
    testBridgeSingleSideAndIdenticalPathsAreUnchanged();
    testBridgeMixSumsBothEffectsOnTargetBoth();
    testBridgeMixWeaponShotsDoNotBounceTarget();
    testBridgePriorityBurstPreemptsWithoutClipping();
    testBridgePriorityContinuousPairNeverFlips();
    testBridgeFirmwareBackendEntersAndRestores();
    testBridgeFirmwareStopWhileEngagedClearsBothSides();
    testBridgeFirmwareLivePolicySwitchRestores();
    testBridgeRecordsRouteWriteFailures();
    return 0;
}
