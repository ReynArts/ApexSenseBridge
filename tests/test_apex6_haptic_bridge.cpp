#ifdef NDEBUG
#undef NDEBUG
#endif

#include "dualsense/Apex6HapticBridge.h"
#include "dualsense/Apex6HapticShaping.h"
#include "dualsense/Apex6PcmGain.h"
#include "dualsense/Apex6TriggerEffect.h"
#include "flydigi/Apex6Protocol.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

int peak(const std::array<std::int8_t, 8>& samples) {
    int result = 0;
    for (const auto sample : samples) result = (std::max)(result, std::abs(static_cast<int>(sample)));
    return result;
}

constexpr unsigned kBurstFrames = asb::dualsense::apex6ImpactFrames(asb::dualsense::Apex6ImpactShape{});

void testNativeTriggerEffects() {
    using namespace asb::dualsense;
    std::array<std::uint8_t, 11> bytes{};
    bytes[0] = 0x21;
    bytes[1] = static_cast<std::uint8_t>((1U << 2U) | (1U << 3U));
    const auto packed = (1U << 6U) | (7U << 9U);
    bytes[3] = static_cast<std::uint8_t>(packed);
    bytes[4] = static_cast<std::uint8_t>(packed >> 8U);
    const auto feedback = decodeApex6TriggerEffect(asb::TriggerSide::Left, bytes, 0);
    assert(feedback && feedback->type == Apex6TriggerType::Feedback);
    assert(feedback->zoneStrengths[2] == 2 && feedback->zoneStrengths[3] == 8);
    Apex6TriggerRenderState weakState{};
    Apex6TriggerRenderState strongState{};
    assert(peak(renderApex6TriggerEffect(*feedback, 60, weakState)) <= 16);
    assert(peak(renderApex6TriggerEffect(*feedback, 90, strongState)) > 40);

    bytes[0] = 0x26;
    bytes[1] = 1U << 3U;
    bytes[3] = bytes[4] = 0;
    bytes[9] = 180;
    bytes[10] = 7;
    const auto vibration = decodeApex6TriggerEffect(asb::TriggerSide::Right, bytes, 0);
    assert(vibration && vibration->frequency == 180);
    Apex6TriggerRenderState vibrationState{};
    // A high requested frequency must never become high motor amplitude.
    assert(peak(renderApex6TriggerEffect(*vibration, 90, vibrationState)) <= 10);
    bytes[9] = 0;
    const auto silentVibration = decodeApex6TriggerEffect(
        asb::TriggerSide::Right, bytes, 0);
    assert(silentVibration);
    Apex6TriggerRenderState silentState{};
    assert(peak(renderApex6TriggerEffect(*silentVibration, 90, silentState)) == 0);

    const std::array<std::uint8_t, 11> referenceVibration{
        0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 150, 0};
    const auto reference = decodeApex6TriggerEffect(
        asb::TriggerSide::Left, referenceVibration, 0);
    assert(reference && reference->frequency == 150);
    assert(reference->zoneStrengths[0] == 8 && reference->zoneStrengths[9] == 8);
    Apex6TriggerRenderState referenceState{};
    assert(peak(renderApex6TriggerEffect(*reference, 255, referenceState)) > 60);
    for (const auto type : {0x21, 0x25, 0x26}) {
        std::array<std::uint8_t, 11> empty{};
        empty[0] = static_cast<std::uint8_t>(type);
        empty[9] = 150;
        Apex6TriggerDecodeError error{};
        const auto decoded = decodeApex6TriggerEffect(
            asb::TriggerSide::Left, empty, 0, &error);
        assert(decoded && decoded->type == Apex6TriggerType::Off);
        assert(error == Apex6TriggerDecodeError::None);
    }

    bytes = {};
    bytes[0] = 0x25;
    bytes[1] = static_cast<std::uint8_t>((1U << 2U) | (1U << 7U));
    bytes[3] = 7;
    const auto weapon = decodeApex6TriggerEffect(asb::TriggerSide::Right, bytes, 0);
    assert(weapon && weapon->type == Apex6TriggerType::Weapon);
    assert(weapon->startZone == 2 && weapon->endZone == 7 && weapon->strength == 8);
    Apex6TriggerRenderState weaponState{};
    assert(peak(renderApex6TriggerEffect(*weapon, 0, weaponState)) == 0);
    assert(peak(renderApex6TriggerEffect(*weapon, 100, weaponState)) == 0);
    for (unsigned frame = 0; frame < kBurstFrames; ++frame) {
        assert(peak(renderApex6TriggerEffect(*weapon, 200, weaponState)) > 0);
    }
    assert(peak(renderApex6TriggerEffect(*weapon, 200, weaponState)) == 0);
    assert(peak(renderApex6TriggerEffect(*weapon, 0, weaponState)) == 0);
    assert(peak(renderApex6TriggerEffect(*weapon, 200, weaponState)) > 0);

    bytes[0] = 0x05;
    const auto off = decodeApex6TriggerEffect(asb::TriggerSide::Right, bytes, 0);
    assert(off && off->type == Apex6TriggerType::Off);
    bytes[0] = 0;
    assert(decodeApex6TriggerEffect(asb::TriggerSide::Right, bytes, 0)->type ==
           Apex6TriggerType::Off);
    bytes[0] = 0x25;
    bytes[2] = 0x80;
    Apex6TriggerDecodeError error{};
    assert(!decodeApex6TriggerEffect(asb::TriggerSide::Right, bytes, 0, &error));
    assert(error == Apex6TriggerDecodeError::InvalidParameters);
    bytes[0] = 0x7F;
    assert(!decodeApex6TriggerEffect(asb::TriggerSide::Right, bytes, 0, &error));
    assert(error == Apex6TriggerDecodeError::UnsupportedType);
}

void testBowEffect() {
    using namespace asb::dualsense;
    std::array<std::uint8_t, 11> bytes{0x22, 1, 1, 55}; // Endfield: zones 0/8, force 8, snap 7.
    Apex6TriggerDecodeError error{};
    const auto bow = decodeApex6TriggerEffect(asb::TriggerSide::Left, bytes, 0, &error);
    assert(bow && bow->type == Apex6TriggerType::Bow);
    assert(error == Apex6TriggerDecodeError::None);
    assert(bow->startZone == 0 && bow->endZone == 8);
    assert(bow->strength == 8 && bow->snapStrength == 7);
    Apex6TriggerRenderState state{};
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        assert(peak(renderApex6TriggerEffect(*bow, 0, state)) == 0);
        assert(peak(renderApex6TriggerEffect(*bow, 100, state)) > 0);
        for (unsigned frame = 0; frame < kBurstFrames; ++frame) {
            assert(peak(renderApex6TriggerEffect(*bow, 220, state)) > 0);
        }
        assert(peak(renderApex6TriggerEffect(*bow, 220, state)) == 0);
        assert(peak(renderApex6TriggerEffect(*bow, 255, state)) == 0);
    }
    // Receiving Bow while already held must not invent a crossing/snap.
    state.reset();
    assert(peak(renderApex6TriggerEffect(*bow, 255, state)) == 0);
    for (const auto mask : {0x001U, 0x103U, 0x201U, 0x801U}) {
        bytes[1] = static_cast<std::uint8_t>(mask);
        bytes[2] = static_cast<std::uint8_t>(mask >> 8U);
        assert(!decodeApex6TriggerEffect(asb::TriggerSide::Left, bytes, 0, &error));
        assert(error == Apex6TriggerDecodeError::InvalidParameters);
    }
    bytes = {0x22, 1, 1, 0x40};
    assert(!decodeApex6TriggerEffect(asb::TriggerSide::Left, bytes, 0, &error));
    bytes = {0x22};
    assert(decodeApex6TriggerEffect(asb::TriggerSide::Left, bytes, 0)->type == Apex6TriggerType::Off);
}

void testContinuousCarrierClock() {
    using namespace asb::dualsense;
    for (const auto type : {Apex6TriggerType::Feedback, Apex6TriggerType::Vibration}) {
        for (const auto frequency : {1, 30, 85, 150, 180, 255}) {
            Apex6TriggerEffect effect{};
            effect.type = type;
            effect.frequency = static_cast<std::uint8_t>(frequency);
            effect.zoneStrengths[9] = 8;
            Apex6TriggerRenderState left{}, right{};
            for (std::uint64_t block = 0; block < 250; ++block) {
                // Different press/release timing and independently installed effects.
                if (block % 19 == 0) right.reset();
                (void)renderApex6TriggerEffect(effect, 255, left, block * 8);
                (void)renderApex6TriggerEffect(effect, block % 3 == 0 ? 0 : 255,
                                               right, block * 8);
                (void)renderApex6TriggerEffect(effect, 255, left, block * 8 + 8);
                (void)renderApex6TriggerEffect(effect, 255, right, block * 8 + 8);
                const auto lt = renderApex6TriggerEffect(effect, 255, left, block * 8 + 16);
                const auto rt = renderApex6TriggerEffect(effect, 255, right, block * 8 + 16);
                assert(lt == rt); // Identical effects remain eligible for Both, at full duty.
            }
        }
    }
}

using Frame = std::array<std::int8_t, 8>;

std::vector<int> hold(int position, int frames) {
    return std::vector<int>(static_cast<std::size_t>(frames), position);
}

std::vector<int> ramp(int from, int to, int frames) {
    std::vector<int> result;
    for (int index = 0; index < frames; ++index) {
        result.push_back(from + (to - from) * (index + 1) / frames);
    }
    return result;
}

void append(std::vector<int>& target, const std::vector<int>& more) {
    target.insert(target.end(), more.begin(), more.end());
}

struct Player {
    asb::dualsense::Apex6TriggerEffect effect;
    asb::dualsense::Apex6TriggerRenderState state{};
    std::uint64_t offset = 0;

    Frame next(int position) {
        const auto frame = asb::dualsense::renderApex6TriggerEffect(
            effect, static_cast<std::uint8_t>(position), state, offset);
        offset += 8;
        return frame;
    }

    std::vector<Frame> play(const std::vector<int>& positions) {
        std::vector<Frame> frames;
        for (const auto position : positions) frames.push_back(next(position));
        return frames;
    }
};

asb::dualsense::Apex6TriggerEffect decodeNative(
    const std::array<std::uint8_t, 11>& bytes) {
    const auto decoded = asb::dualsense::decodeApex6TriggerEffect(
        asb::TriggerSide::Right, bytes, 0);
    assert(decoded);
    return *decoded;
}

asb::dualsense::Apex6TriggerEffect legacyEffect(
    asb::TriggerMode mode, std::array<std::uint8_t, 5> params) {
    asb::dualsense::Apex6TriggerEffect effect{};
    effect.type = asb::dualsense::Apex6TriggerType::Legacy;
    effect.legacy = asb::ForceTriggerCommand{asb::TriggerSide::Right, mode, params};
    return effect;
}

std::array<std::uint8_t, 11> zoneBytes(
    std::uint8_t type, unsigned mask, const std::array<unsigned, 10>& strengths,
    std::uint8_t frequency = 0) {
    std::array<std::uint8_t, 11> bytes{};
    bytes[0] = type;
    bytes[1] = static_cast<std::uint8_t>(mask);
    bytes[2] = static_cast<std::uint8_t>(mask >> 8U);
    std::uint32_t packed = 0;
    for (unsigned zone = 0; zone < 10; ++zone) {
        if ((mask & (1U << zone)) != 0) {
            packed |= static_cast<std::uint32_t>(strengths[zone] - 1U) << (zone * 3U);
        }
    }
    bytes[3] = static_cast<std::uint8_t>(packed);
    bytes[4] = static_cast<std::uint8_t>(packed >> 8U);
    bytes[5] = static_cast<std::uint8_t>(packed >> 16U);
    bytes[6] = static_cast<std::uint8_t>(packed >> 24U);
    bytes[9] = frequency;
    return bytes;
}

int settledPeak(const asb::dualsense::Apex6TriggerEffect& effect, int position) {
    Player player{effect};
    int result = 0;
    for (int frame = 0; frame < 12; ++frame) {
        const auto samples = player.next(position);
        if (frame >= 4) result = (std::max)(result, peak(samples));
    }
    return result;
}

std::vector<std::vector<int>> burstPeaks(const std::vector<Frame>& frames) {
    std::vector<std::vector<int>> bursts;
    std::vector<int> current;
    for (const auto& frame : frames) {
        const auto framePeak = peak(frame);
        if (framePeak > 0) {
            current.push_back(framePeak);
        } else if (!current.empty()) {
            bursts.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) bursts.push_back(current);
    return bursts;
}

std::vector<int> breakSequence() {
    std::vector<int> positions;
    append(positions, hold(0, 10));
    append(positions, ramp(0, 255, 100));
    append(positions, hold(255, 40));
    for (int index = 0; index < 20; ++index) append(positions, {175, 200});
    append(positions, ramp(255, 60, 50));
    append(positions, ramp(60, 255, 60));
    append(positions, ramp(255, 0, 50));
    append(positions, hold(0, 5));
    append(positions, ramp(0, 255, 100));
    append(positions, hold(255, 20));
    return positions;
}

int expectedBurstDrive(unsigned force) {
    using namespace asb::dualsense;
    const double fraction = (std::min)(1.0, force / double(kApex6TriggerBurstFullForce));
    return static_cast<int>(std::lround(
        kApex6TriggerBurstMinimumDrive +
        (kApex6TriggerNativeMaximumDrive - kApex6TriggerBurstMinimumDrive) * fraction));
}

void checkOneShotBursts(const std::vector<std::vector<int>>& bursts, int drive) {
    using namespace asb::dualsense;
    assert(bursts.size() == 2);
    for (const auto& burst : bursts) {
        assert(burst.size() == kBurstFrames);
        assert(burst[0] > burst.back());
        assert(burst[0] <= kApex6TriggerNativeMaximumDrive);
        assert(burst[0] <= drive && burst[0] >= drive * 9 / 10);
    }
}

void testSnapIsOneShotBurst() {
    using namespace asb::dualsense;
    for (const unsigned strength : {1U, 4U, 8U}) {
        const auto weapon = decodeNative(
            {0x25, 0x84, 0x00, static_cast<std::uint8_t>(strength - 1U)});
        Player player{weapon};
        const auto frames = player.play(breakSequence());
        checkOneShotBursts(burstPeaks(frames), expectedBurstDrive(strength * 8U));
        assert(player.state.burstsStarted == 2);
        for (const auto& frame : frames) assert(peak(frame) <= kApex6TriggerNativeMaximumDrive);
    }

    const auto weak = decodeNative({0x25, 0x84, 0x00, 0});
    const auto strong = decodeNative({0x25, 0x84, 0x00, 7});
    Player weakPlayer{weak};
    Player strongPlayer{strong};
    const auto weakPeaks = burstPeaks(weakPlayer.play(breakSequence()));
    const auto strongPeaks = burstPeaks(strongPlayer.play(breakSequence()));
    assert(strongPeaks[0][0] > weakPeaks[0][0]);
    assert(weakPeaks[0][0] >= 50);
}

void testLegacyBreakAndLockAreBursts() {
    using namespace asb::dualsense;
    for (const unsigned force : {8U, 64U, 130U}) {
        Player player{legacyEffect(asb::TriggerMode::SniperBreak,
                                   {38, 96, static_cast<std::uint8_t>(force), 0, 0})};
        const auto frames = player.play(breakSequence());
        checkOneShotBursts(burstPeaks(frames), expectedBurstDrive(force));
        assert(player.state.burstsStarted == 2);
    }
    Player noForce{legacyEffect(asb::TriggerMode::SniperBreak, {38, 96, 0, 0, 0})};
    for (const auto& frame : noForce.play(breakSequence())) assert(peak(frame) == 0);

    Player lock{legacyEffect(asb::TriggerMode::Lock, {100, 0, 0, 0, 0})};
    std::vector<int> positions;
    append(positions, hold(0, 10));
    append(positions, ramp(0, 255, 60));
    append(positions, hold(255, 40));
    append(positions, ramp(255, 128, 20));
    append(positions, ramp(128, 255, 20));
    append(positions, ramp(255, 60, 40));
    append(positions, ramp(60, 255, 60));
    append(positions, hold(255, 20));
    const auto frames = lock.play(positions);
    const auto bursts = burstPeaks(frames);
    assert(bursts.size() == 2);
    for (const auto& burst : bursts) {
        assert(burst.size() == kBurstFrames);
        assert(burst[0] >= kApex6TriggerNativeMaximumDrive * 9 / 10);
        assert(burst[0] <= kApex6TriggerNativeMaximumDrive);
    }
    assert(lock.state.burstsStarted == 2);
}

void testLegacyContinuousModesStartSmoothly() {
    using namespace asb::dualsense;
    const auto race = legacyEffect(asb::TriggerMode::Race, {20, 160, 0, 0, 0});
    int previous = 0;
    for (int position = 0; position <= 255; ++position) {
        const auto level = settledPeak(race, position);
        assert(level + 1 >= previous);
        assert(level <= kApex6TriggerNativeMaximumDrive);
        previous = level;
    }
    assert(settledPeak(race, 20) == 0);
    assert(settledPeak(race, 255) >= kApex6TriggerNativeMaximumDrive - 2);
    assert(settledPeak(race, 33) > 0 && settledPeak(race, 33) < kApex6TriggerNativeMaximumDrive - 10);

    for (const auto mode : {asb::TriggerMode::RecoilRattle, asb::TriggerMode::Vibration}) {
        const auto rattle = legacyEffect(mode, {50, 1, 60, 45, 0});
        assert(settledPeak(rattle, 60) == 0);
        assert(settledPeak(rattle, 255) >= 57 && settledPeak(rattle, 255) <= 60);
    }
}

void testProgressiveFeedbackFollowsProfile() {
    using namespace asb::dualsense;
    std::array<unsigned, 10> slope{};
    for (unsigned zone = 0; zone < 10; ++zone) slope[zone] = 1 + zone * 7 / 9;
    const auto progressive = decodeNative(zoneBytes(0x21, 0x3FF, slope));
    int previous = 0;
    int largestStep = 0;
    int first = -1;
    for (int position = 0; position <= 255; ++position) {
        const auto level = settledPeak(progressive, position);
        if (first < 0) {
            first = level;
            previous = level;
        }
        assert(level + 1 >= previous);
        largestStep = (std::max)(largestStep, std::abs(level - previous));
        previous = level;
    }
    assert(largestStep <= 2);
    assert(first >= 7 && first <= 8);
    assert(settledPeak(progressive, 255) >= 62);
    assert(settledPeak(progressive, 13) >= 6 && settledPeak(progressive, 13) <= 9);
    assert(settledPeak(progressive, 128) > settledPeak(progressive, 64));
    assert(settledPeak(progressive, 192) > settledPeak(progressive, 128));

    std::array<unsigned, 10> sparse{};
    sparse[1] = sparse[2] = 8;
    sparse[6] = sparse[7] = 4;
    const auto gapped = decodeNative(zoneBytes(
        0x21, (1U << 1) | (1U << 2) | (1U << 6) | (1U << 7), sparse));
    int sparseStep = 0;
    int sparsePrevious = 0;
    for (int position = 0; position <= 255; ++position) {
        const auto level = settledPeak(gapped, position);
        sparseStep = (std::max)(sparseStep, std::abs(level - sparsePrevious));
        sparsePrevious = level;
        if (position < 25 || (position > 78 && position < 153) || position > 205) {
            assert(level == 0);
        }
    }
    assert(sparseStep <= 8);
    assert(settledPeak(gapped, 38) >= 62);
    assert(settledPeak(gapped, 166) >= 30 && settledPeak(gapped, 166) <= 33);

    std::array<unsigned, 10> flat{};
    for (auto& value : flat) value = 6;
    const auto uniform = decodeNative(zoneBytes(0x21, 0x3F8, flat));
    assert(settledPeak(uniform, 76) == 0);
    assert(settledPeak(uniform, 200) >= 47 && settledPeak(uniform, 200) <= 48);
    assert(settledPeak(uniform, 83) > 10 && settledPeak(uniform, 83) < 40);
    assert(settledPeak(uniform, 95) >= 47);
}

int zeroCrossings(const std::vector<Frame>& frames) {
    int crossings = 0;
    int previous = 0;
    for (const auto& frame : frames) {
        for (const auto sample : frame) {
            if (sample == 0) continue;
            if (previous != 0 && (sample > 0) != (previous > 0)) ++crossings;
            previous = sample;
        }
    }
    return crossings;
}

void testVibrationHonoursZoneStrengthsAndFrequency() {
    using namespace asb::dualsense;
    std::array<unsigned, 10> strengths{};
    strengths[2] = 2;
    strengths[3] = 8;
    strengths[4] = 4;
    const auto vibration = decodeNative(zoneBytes(0x26, 0x1C, strengths, 60));
    assert(settledPeak(vibration, 64) >= 19 && settledPeak(vibration, 64) <= 20);
    assert(settledPeak(vibration, 90) >= 77 && settledPeak(vibration, 90) <= 80);
    assert(settledPeak(vibration, 115) >= 39 && settledPeak(vibration, 115) <= 41);
    assert(settledPeak(vibration, 77) >= 47 && settledPeak(vibration, 77) <= 53);
    int previous = 0;
    for (int position = 64; position <= 90; ++position) {
        const auto level = settledPeak(vibration, position);
        assert(level + 1 >= previous);
        previous = level;
    }
    assert(settledPeak(vibration, 20) == 0 && settledPeak(vibration, 200) == 0);

    std::array<unsigned, 10> flat{};
    for (auto& value : flat) value = 5;
    for (const unsigned frequency : {25U, 50U, 100U, 160U}) {
        const auto effect = decodeNative(zoneBytes(0x26, 0x3FF, flat, static_cast<std::uint8_t>(frequency)));
        Player player{effect};
        const auto frames = player.play(hold(200, 120));
        const auto expected = static_cast<int>(2 * frequency * 960 / 1000);
        assert(std::abs(zeroCrossings(frames) - expected) <= 4);
        assert(settledPeak(effect, 200) >= 48 && settledPeak(effect, 200) <= 50);
    }
}

void testBowRampsThenBurstsThenStaysSilent() {
    using namespace asb::dualsense;
    const auto bow = decodeNative({0x22, 0x82, 0x00, 55});
    assert(bow.startZone == 1 && bow.endZone == 7);
    int previous = 0;
    for (int position = 0; position < 180; ++position) {
        const auto level = settledPeak(bow, position);
        assert(level + 1 >= previous);
        previous = level;
    }
    assert(previous >= 44 && previous <= 48);
    assert(settledPeak(bow, 10) == 0);

    std::vector<int> positions;
    append(positions, hold(0, 10));
    append(positions, ramp(0, 255, 100));
    const auto crossIndex = static_cast<std::size_t>(
        std::find_if(positions.begin(), positions.end(),
                     [](int position) { return position >= 180; }) - positions.begin());
    append(positions, hold(255, 30));
    const auto releaseStart = positions.size();
    append(positions, ramp(255, 0, 60));
    const auto releaseEnd = positions.size();
    append(positions, hold(0, 5));
    const auto secondStart = positions.size();
    append(positions, ramp(0, 255, 100));

    Player player{bow};
    const auto frames = player.play(positions);
    int rampPeak = 0;
    for (std::size_t index = 0; index < crossIndex; ++index) {
        rampPeak = (std::max)(rampPeak, peak(frames[index]));
    }
    const auto burstFrames = kBurstFrames;
    assert(peak(frames[crossIndex]) > rampPeak);
    assert(peak(frames[crossIndex]) >= expectedBurstDrive(7U * 8U) * 9 / 10);
    for (std::size_t offset = 0; offset < burstFrames; ++offset) {
        assert(peak(frames[crossIndex + offset]) > 0);
    }
    assert(peak(frames[crossIndex]) > peak(frames[crossIndex + burstFrames - 1]));
    for (std::size_t index = crossIndex + burstFrames; index < releaseEnd; ++index) {
        if (index >= releaseStart && positions[index] <= 30) break;
        assert(peak(frames[index]) == 0);
    }
    int secondRamp = 0;
    std::size_t secondBurst = 0;
    for (std::size_t index = secondStart; index < frames.size(); ++index) {
        if (positions[index] < 180) secondRamp = (std::max)(secondRamp, peak(frames[index]));
        else if (secondBurst == 0) secondBurst = index;
    }
    assert(secondRamp > 40);
    assert(peak(frames[secondBurst]) > secondRamp);
    assert(player.state.burstsStarted == 2);
}

void testRetargetKeepsBurstWithoutRetrigger() {
    using namespace asb::dualsense;
    Player player{decodeNative({0x25, 0x84, 0x00, 7})};
    assert(peak(player.next(0)) == 0);
    assert(peak(player.next(200)) > 0);
    player.state.retarget();
    for (unsigned frame = 1; frame < kBurstFrames; ++frame) {
        assert(peak(player.next(200)) > 0);
    }
    assert(peak(player.next(200)) == 0);
    assert(player.state.burstsStarted == 1);

    Player cut{decodeNative({0x25, 0x84, 0x00, 7})};
    assert(peak(cut.next(0)) == 0);
    assert(peak(cut.next(200)) > 0);
    cut.effect = Apex6TriggerEffect{};
    assert(peak(cut.next(200)) == 0);
    assert(peak(cut.next(200)) == 0);
}

std::uint32_t nextRandom(std::uint32_t& seed) {
    seed = seed * 1664525U + 1013904223U;
    return seed >> 8U;
}

void testRendererBoundsAndContinuity() {
    using namespace asb::dualsense;
    std::array<unsigned, 10> slope{};
    std::array<unsigned, 10> reversed{};
    for (unsigned zone = 0; zone < 10; ++zone) {
        slope[zone] = 1 + zone * 7 / 9;
        reversed[zone] = 8 - zone * 7 / 9;
    }
    const std::vector<std::pair<Apex6TriggerEffect, Apex6TriggerEffect>> continuous{
        {decodeNative(zoneBytes(0x21, 0x3FF, slope)),
         decodeNative(zoneBytes(0x21, 0x3FF, reversed))},
        {decodeNative(zoneBytes(0x26, 0x3FF, slope, 90)),
         decodeNative(zoneBytes(0x26, 0x3FF, reversed, 90))},
    };
    for (const auto& [first, second] : continuous) {
        const double frequency = first.type == Apex6TriggerType::Feedback
            ? kApex6TriggerCarrierFrequency : 90.0;
        const double drive = first.type == Apex6TriggerType::Feedback
            ? 8.0 * kApex6TriggerFeedbackDrivePerLevel : 8.0 * kApex6TriggerVibrationDrivePerLevel;
        const int bound = static_cast<int>(
            drive * 2.0 * 3.14159265358979 * frequency / 1000.0 + drive / 8.0 + 2.0);
        Player player{first};
        std::uint32_t seed = 12345;
        int position = 0;
        int previousSample = 0;
        for (int frame = 0; frame < 4000; ++frame) {
            if (nextRandom(seed) % 41 == 0) position = static_cast<int>(nextRandom(seed) % 256);
            else position = (std::clamp)(position + static_cast<int>(nextRandom(seed) % 41) - 20, 0, 255);
            if (frame % 53 == 0) {
                player.effect = (frame / 53) % 2 == 0 ? first : second;
                player.state.retarget();
            }
            for (const auto sample : player.next(position)) {
                assert(std::abs(int(sample)) <= kApex6TriggerNativeMaximumDrive);
                assert(std::abs(int(sample) - previousSample) <= bound);
                previousSample = sample;
            }
        }
    }

    const std::vector<Apex6TriggerEffect> all{
        decodeNative({0x25, 0x84, 0x00, 7}),
        decodeNative({0x22, 0x82, 0x00, 55}),
        decodeNative(zoneBytes(0x21, 0x3FF, slope)),
        decodeNative(zoneBytes(0x26, 0x3FF, reversed, 130)),
        legacyEffect(asb::TriggerMode::Race, {0, 255, 0, 0, 0}),
        legacyEffect(asb::TriggerMode::SniperBreak, {0, 255, 255, 0, 0}),
        legacyEffect(asb::TriggerMode::RecoilRattle, {255, 255, 255, 255, 0}),
        legacyEffect(asb::TriggerMode::Vibration, {0, 1, 255, 0, 0}),
        legacyEffect(asb::TriggerMode::Lock, {255, 255, 255, 255, 255}),
        Apex6TriggerEffect{},
    };
    for (const auto& effect : all) {
        Player player{effect};
        std::uint32_t seed = 987;
        for (int frame = 0; frame < 4000; ++frame) {
            if (frame % 29 == 0) player.state.retarget();
            if (frame % 997 == 0) player.state.reset();
            const auto position = static_cast<int>(nextRandom(seed) % 256);
            for (const auto sample : player.next(position)) {
                assert(std::abs(int(sample)) <= kApex6TriggerNativeMaximumDrive);
            }
        }
    }
}

std::vector<int> pressReleasePress() {
    std::vector<int> positions;
    append(positions, hold(0, 10));
    append(positions, ramp(0, 255, 60));
    append(positions, hold(255, 200));
    append(positions, ramp(255, 0, 40));
    append(positions, hold(0, 10));
    append(positions, ramp(0, 255, 60));
    append(positions, hold(255, 200));
    return positions;
}

struct SceneFacts {
    asb::dualsense::Apex6WeaponPreset preset;
    unsigned gain;
    unsigned waveMilliseconds;
    unsigned carrierFrom;
    unsigned carrierTo;
    double carrierLow;
    double carrierHigh;
};

constexpr std::array<SceneFacts, 11> kSceneFacts{{
    {asb::dualsense::Apex6WeaponPreset::MachineGun, 29, 94, 0, 40, 55.0, 70.0},
    {asb::dualsense::Apex6WeaponPreset::Rifle, 32, 84, 0, 40, 55.0, 70.0},
    {asb::dualsense::Apex6WeaponPreset::SubmachineGun, 40, 60, 0, 30, 50.0, 65.0},
    {asb::dualsense::Apex6WeaponPreset::LightMachineGun, 27, 94, 0, 60, 60.0, 72.0},
    {asb::dualsense::Apex6WeaponPreset::Pistol, 28, 200, 82, 150, 60.0, 80.0},
    {asb::dualsense::Apex6WeaponPreset::Shotgun, 28, 320, 100, 200, 48.0, 64.0},
    {asb::dualsense::Apex6WeaponPreset::Sword, 36, 185, 0, 50, 205.0, 250.0},
    {asb::dualsense::Apex6WeaponPreset::Blade, 40, 155, 0, 40, 200.0, 240.0},
    {asb::dualsense::Apex6WeaponPreset::Dagger, 40, 125, 40, 75, 205.0, 255.0},
    {asb::dualsense::Apex6WeaponPreset::Axe, 40, 270, 0, 200, 64.0, 76.0},
    {asb::dualsense::Apex6WeaponPreset::Club, 36, 180, 0, 140, 62.0, 74.0},
}};

constexpr std::array<double, 4> kPresetLevels{0.5, 0.7, 0.85, 1.0};

int presetDrive(const asb::dualsense::Apex6TriggerEffect& effect) {
    return effect.type == asb::dualsense::Apex6TriggerType::Rhythm ? effect.rhythm.drive
                                                                   : effect.impactDrive;
}

int expectedPresetPeak(const asb::dualsense::Apex6TriggerEffect& effect) {
    return (std::min)(presetDrive(effect), int(asb::dualsense::kApex6TriggerMaximumDrive));
}

std::vector<int> presetSamples(const asb::dualsense::Apex6TriggerEffect& effect,
                               std::size_t count) {
    Player player{effect};
    (void)player.next(0);
    std::vector<int> samples;
    while (samples.size() < count) {
        for (const auto sample : player.next(255)) samples.push_back(sample);
    }
    samples.resize(count);
    return samples;
}

double zeroCrossingFrequency(const std::vector<int>& samples, unsigned from, unsigned to) {
    std::vector<double> crossings;
    std::size_t last = to;
    for (std::size_t index = from; index < to; ++index) {
        if (samples[index] == 0) continue;
        if (last != to && (samples[index] > 0) != (samples[last] > 0)) {
            const double a = samples[last];
            const double b = samples[index];
            crossings.push_back(double(last) + (double(index) - double(last)) * a / (a - b));
        }
        last = index;
    }
    assert(crossings.size() >= 3);
    return 1000.0 * double(crossings.size() - 1) / (2.0 * (crossings.back() - crossings.front()));
}

double rootMeanSquare(const std::vector<int>& samples, std::size_t from, std::size_t to) {
    double sum = 0.0;
    for (std::size_t index = from; index < to; ++index) sum += double(samples[index]) * samples[index];
    return std::sqrt(sum / static_cast<double>(to - from));
}

void testPresetTable() {
    using namespace asb::dualsense;
    for (std::size_t index = 0; index < kApex6WeaponPresetCount; ++index) {
        const auto preset = static_cast<Apex6WeaponPreset>(index);
        const auto name = apex6WeaponPresetName(preset);
        assert(apex6WeaponPresetFromName(name) == preset);
        int previousDrive = 0;
        for (unsigned level = 1; level <= 4; ++level) {
            const auto effect = makeApex6WeaponPreset(preset, level);
            const int drive = presetDrive(effect);
            assert(drive >= 40 && drive <= 160);
            assert(drive >= previousDrive);
            previousDrive = drive;
            assert(effect.type == (apex6WeaponPresetIsTrain(preset) ? Apex6TriggerType::Rhythm
                                                                   : Apex6TriggerType::Impact));
            if (apex6WeaponPresetIsTrain(preset)) {
                assert(apex6ImpactTotalSamples(effect.rhythm.pulse) <= effect.rhythm.periodSamples);
                assert(effect.rhythm.activeFrom == 80 && effect.rhythm.activeTo == 255);
                assert(effect.rhythm.pulse.tailSamples == 0);
            } else {
                assert(effect.rearmPosition < effect.triggerPosition || effect.triggerPosition == 0);
                assert(effect.impact.tailSamples == (preset == Apex6WeaponPreset::SniperRifle ? 1000 : 0));
            }
        }
        assert(makeApex6WeaponPreset(preset, 0) == makeApex6WeaponPreset(preset, 1));
        assert(makeApex6WeaponPreset(preset, 77) == makeApex6WeaponPreset(preset, 4));
    }
    for (const auto* alias : {"Machine Gun", "machine-gun", "MACHINEGUN", "machine_gun"}) {
        assert(apex6WeaponPresetFromName(alias) == Apex6WeaponPreset::MachineGun);
    }
    assert(apex6WeaponPresetFromName("Submachine Gun") == Apex6WeaponPreset::SubmachineGun);
    assert(apex6WeaponPresetFromName("light machine gun") == Apex6WeaponPreset::LightMachineGun);
    assert(apex6WeaponPresetFromName("Sniper Rifle") == Apex6WeaponPreset::SniperRifle);
    for (const auto* rejected : {"", "sniper", "weapon", "bow", "resistance", "vibration", "gun"}) {
        assert(!apex6WeaponPresetFromName(rejected));
    }

    const auto pistol = makeApex6WeaponPreset(Apex6WeaponPreset::Pistol, 4);
    const auto sniper = makeApex6WeaponPreset(Apex6WeaponPreset::SniperRifle, 4);
    assert(pistol.triggerPosition == 50 && sniper.triggerPosition == 178);
    assert(makeApex6WeaponPreset(Apex6WeaponPreset::Pistol, 4, std::uint8_t{80}).triggerPosition == 80);
    assert(makeApex6WeaponPreset(Apex6WeaponPreset::Axe, 4).triggerPosition == 80);
    for (const auto preset : {Apex6WeaponPreset::Sword, Apex6WeaponPreset::Blade,
                              Apex6WeaponPreset::Axe, Apex6WeaponPreset::Club}) {
        assert(makeApex6WeaponPreset(preset, 4).triggerPosition == 80);
    }
    assert(makeApex6WeaponPreset(Apex6WeaponPreset::Shotgun, 4).triggerPosition == 50);
    assert(makeApex6WeaponPreset(Apex6WeaponPreset::Dagger, 4).triggerPosition == 50);
    const auto shotgun = makeApex6WeaponPreset(Apex6WeaponPreset::Shotgun, 4);
    assert(apex6ImpactTotalSamples(shotgun.impact) > apex6ImpactTotalSamples(pistol.impact));
    assert(shotgun.impactDrive == pistol.impactDrive);
    for (unsigned level = 1; level <= 4; ++level) {
        const auto rendered = makeApex6WeaponPreset(Apex6WeaponPreset::SniperRifle, level);
        assert(rendered.impactDrive == std::lround(kApex6TriggerNativeMaximumDrive * kPresetLevels[level - 1]));
        assert(rendered.impact.segmentCount == 0 && rendered.impact.frequency == 100.0);
        assert(apex6ImpactBodySamples(rendered.impact) == 81);
    }

    for (std::size_t index = 0; index < kApex6WeaponPresetCount; ++index) {
        for (unsigned level = 1; level <= 4; ++level) {
            Player player{makeApex6WeaponPreset(static_cast<Apex6WeaponPreset>(index), level)};
            std::uint32_t seed = 4242U + static_cast<std::uint32_t>(index);
            for (int frame = 0; frame < 2500; ++frame) {
                if (frame % 61 == 0) player.state.retarget();
                for (const auto sample : player.next(static_cast<int>(nextRandom(seed) % 256))) {
                    assert(std::abs(int(sample)) <= kApex6TriggerMaximumDrive);
                }
            }
        }
    }
}

void testPositionOneShotPlaysOncePerPress() {
    using namespace asb::dualsense;
    for (const auto preset : {Apex6WeaponPreset::Pistol, Apex6WeaponPreset::Shotgun,
                              Apex6WeaponPreset::Sword, Apex6WeaponPreset::Blade,
                              Apex6WeaponPreset::Dagger, Apex6WeaponPreset::Axe,
                              Apex6WeaponPreset::Club}) {
        const auto effect = makeApex6WeaponPreset(preset, 4);
        Player player{effect};
        std::vector<Frame> frames;
        std::vector<std::size_t> starts;
        for (const auto position : pressReleasePress()) {
            const auto before = player.state.burstsStarted;
            frames.push_back(player.next(position));
            if (player.state.burstsStarted > before) starts.push_back(frames.size() - 1);
        }
        assert(starts.size() == 2 && player.state.burstsStarted == 2);
        const auto length = apex6ImpactFrames(effect.impact);
        for (const auto start : starts) {
            int bodyPeak = 0;
            for (std::size_t index = start; index < start + length; ++index) {
                bodyPeak = (std::max)(bodyPeak, peak(frames[index]));
            }
            assert(peak(frames[start]) > 0);
            assert(bodyPeak <= expectedPresetPeak(effect));
            assert(bodyPeak >= expectedPresetPeak(effect) * 9 / 10);
            for (std::size_t index = start + length; index < start + length + 20; ++index) {
                assert(peak(frames[index]) == 0);
            }
        }
    }

    Player jitter{makeApex6WeaponPreset(Apex6WeaponPreset::Pistol, 4)};
    std::vector<int> positions;
    append(positions, hold(0, 5));
    append(positions, ramp(0, 120, 20));
    for (int index = 0; index < 60; ++index) append(positions, {52, 120, 49, 90});
    const auto bursts = burstPeaks(jitter.play(positions));
    assert(bursts.size() == 1 && jitter.state.burstsStarted == 1);

    Player rearm{makeApex6WeaponPreset(Apex6WeaponPreset::Pistol, 4)};
    positions.clear();
    append(positions, hold(0, 5));
    append(positions, ramp(0, 120, 20));
    append(positions, hold(120, 20));
    append(positions, ramp(120, 46, 10));
    append(positions, ramp(46, 120, 10));
    append(positions, hold(120, 20));
    append(positions, ramp(120, 20, 10));
    append(positions, ramp(20, 120, 10));
    append(positions, hold(120, 20));
    (void)rearm.play(positions);
    assert(rearm.state.burstsStarted == 2);
}

void testPresetsFollowTheSceneShapes() {
    using namespace asb::dualsense;
    for (const auto& facts : kSceneFacts) {
        const bool train = apex6WeaponPresetIsTrain(facts.preset);
        for (unsigned level = 1; level <= 4; ++level) {
            const auto effect = makeApex6WeaponPreset(facts.preset, level);
            assert(presetDrive(effect) ==
                   std::lround(facts.gain * kApex6TriggerPresetDrivePerGain * kPresetLevels[level - 1]));
            const auto& shape = train ? effect.rhythm.pulse : effect.impact;
            const auto length = apex6ImpactTotalSamples(shape);
            const auto samples = presetSamples(effect, length);
            int samplePeak = 0;
            for (const auto sample : samples) samplePeak = (std::max)(samplePeak, std::abs(sample));
            assert(samplePeak <= expectedPresetPeak(effect));
            assert(samplePeak >= expectedPresetPeak(effect) * 9 / 10);
        }
        const auto effect = makeApex6WeaponPreset(facts.preset, 4);
        const auto& shape = train ? effect.rhythm.pulse : effect.impact;
        assert(shape.segmentCount >= 1 && shape.segmentCount <= kApex6ImpactMaximumSegments);
        assert(shape.tailSamples == 0);
        const auto length = apex6ImpactTotalSamples(shape);
        assert(length <= facts.waveMilliseconds && length * 2 >= facts.waveMilliseconds);
        const auto samples = presetSamples(effect, length);
        const double carrier = zeroCrossingFrequency(samples, facts.carrierFrom, facts.carrierTo);
        assert(carrier >= facts.carrierLow && carrier <= facts.carrierHigh);
        int samplePeak = 0;
        for (const auto sample : samples) samplePeak = (std::max)(samplePeak, std::abs(sample));
        std::size_t attack = 0;
        while (std::abs(samples[attack]) * 2 < samplePeak) ++attack;
        assert(attack <= 8);
        assert(rootMeanSquare(samples, length - length / 4, length) <
               0.5 * rootMeanSquare(samples, 0, length / 4));
        int finalPeak = 0;
        for (std::size_t index = length - length / 10; index < length; ++index) {
            finalPeak = (std::max)(finalPeak, std::abs(samples[index]));
        }
        assert(finalPeak * 3 < samplePeak);
    }
}

void testSniperTailDecaysForAboutASecond() {
    using namespace asb::dualsense;
    const auto effect = makeApex6WeaponPreset(Apex6WeaponPreset::SniperRifle, 4);
    Player player{effect};
    std::vector<int> positions;
    append(positions, hold(0, 10));
    append(positions, ramp(0, 255, 40));
    append(positions, hold(255, 200));
    std::vector<Frame> frames;
    std::vector<bool> priority;
    for (const auto position : positions) {
        frames.push_back(player.next(position));
        priority.push_back(player.state.burstInFrame);
    }
    assert(player.state.burstsStarted == 1);
    std::size_t first = 0;
    while (peak(frames[first]) == 0) ++first;
    const auto body = apex6ImpactBodySamples(effect.impact);
    assert(peak(frames[first]) >= effect.impactDrive * 9 / 10);
    const auto bodyFrames = (body + 7U) / 8U;
    for (std::size_t index = first; index < first + bodyFrames - 1; ++index) {
        assert(priority[index]);
    }
    for (std::size_t index = first + bodyFrames + 1; index < first + apex6ImpactFrames(effect.impact); ++index) {
        assert(!priority[index]);
    }
    int previousWindow = 1000;
    int lastWindow = 0;
    for (std::size_t window = 0; window < 8; ++window) {
        int windowPeak = 0;
        for (std::size_t index = 0; index < 12; ++index) {
            const auto frame = first + bodyFrames + 2 + window * 12 + index;
            windowPeak = (std::max)(windowPeak, peak(frames[frame]));
        }
        assert(windowPeak <= previousWindow);
        assert(windowPeak <= static_cast<int>(effect.impactDrive * effect.impact.tailLevel) + 2);
        if (window == 0) assert(windowPeak >= 20);
        if (window < 6) assert(windowPeak > 0);
        previousWindow = windowPeak;
        lastWindow = windowPeak;
    }
    assert(lastWindow <= 5);
    const auto totalFrames = apex6ImpactFrames(effect.impact);
    assert(totalFrames >= 130 && totalFrames <= 140);
    assert(peak(frames[first + totalFrames]) == 0);
}

std::vector<std::int8_t> flatten(const std::vector<Frame>& frames) {
    std::vector<std::int8_t> samples;
    for (const auto& frame : frames) samples.insert(samples.end(), frame.begin(), frame.end());
    return samples;
}


void testRhythmTrainFollowsTheSceneCadence() {
    using namespace asb::dualsense;
    constexpr int kContinuityBound = 64;
    for (const auto& facts : kSceneFacts) {
        if (!apex6WeaponPresetIsTrain(facts.preset)) continue;
        const auto effect = makeApex6WeaponPreset(facts.preset, 4);
        const auto period = effect.rhythm.periodSamples;
        assert(period == facts.waveMilliseconds);
        assert(apex6ImpactTotalSamples(effect.rhythm.pulse) <= period);
        Player player{effect};
        std::vector<int> positions;
        append(positions, hold(0, 10));
        append(positions, hold(79, 10));
        const auto pressAt = positions.size();
        for (int index = 0; index < 300; ++index) positions.push_back(100 + (index * 37) % 156);
        const auto releaseAt = positions.size();
        append(positions, hold(0, 20));
        const auto frames = player.play(positions);
        const auto samples = flatten(frames);
        for (std::size_t index = 0; index < pressAt * 8; ++index) assert(samples[index] == 0);
        const std::vector<int> held(samples.begin() + pressAt * 8, samples.begin() + releaseAt * 8);
        assert(held[0] == 0);
        for (std::size_t index = period; index < held.size(); ++index) {
            assert(held[index] == held[index - period]);
        }
        int pulsePeak = 0;
        for (std::size_t index = 0; index < period; ++index) {
            pulsePeak = (std::max)(pulsePeak, std::abs(held[index]));
        }
        assert(pulsePeak <= expectedPresetPeak(effect));
        assert(pulsePeak >= expectedPresetPeak(effect) * 9 / 10);
        for (std::size_t index = 1; index < held.size(); ++index) {
            assert(std::abs(held[index] - held[index - 1]) <= kContinuityBound);
        }
        const auto tail = flatten(std::vector<Frame>(frames.end() - 3, frames.end()));
        for (const auto sample : tail) assert(sample == 0);
    }
    const auto cadence = [](Apex6WeaponPreset preset) {
        return 1000.0 / makeApex6WeaponPreset(preset, 4).rhythm.periodSamples;
    };
    assert(std::abs(cadence(Apex6WeaponPreset::MachineGun) - 10.6) < 0.1);
    assert(std::abs(cadence(Apex6WeaponPreset::Rifle) - 11.9) < 0.1);
    assert(std::abs(cadence(Apex6WeaponPreset::SubmachineGun) - 16.7) < 0.1);
    assert(std::abs(cadence(Apex6WeaponPreset::LightMachineGun) - 10.6) < 0.1);

    Player restart{makeApex6WeaponPreset(Apex6WeaponPreset::MachineGun, 4)};
    std::vector<int> positions;
    append(positions, hold(200, 20));
    append(positions, hold(0, 4));
    append(positions, hold(200, 20));
    const auto frames = restart.play(positions);
    assert(peak(frames[20]) == 0 && peak(frames[23]) == 0);
    assert(peak(frames[24]) > 0);
    assert(frames[24][0] == 0 && frames[24][1] != 0);
}

void testMachineAndGallopingDecodeToRhythm() {
    using namespace asb::dualsense;
    const auto machine = decodeNative({0x27, 0x84, 0x00, 7 | (2 << 3), 60, 12});
    assert(machine.type == Apex6TriggerType::Rhythm && machine.rhythm.modulated);
    assert(machine.rhythm.periodSamples == 1200 && machine.rhythm.pulse.frequency == 60.0);
    assert(machine.rhythm.levelA == 84.0 && machine.rhythm.levelB == 24.0);
    assert(machine.rhythm.activeFrom == 52 && machine.rhythm.activeTo == 204);
    Player player{machine};
    std::vector<int> positions;
    append(positions, hold(30, 10));
    append(positions, hold(120, 160));
    append(positions, hold(230, 10));
    const auto samples = flatten(player.play(positions));
    for (std::size_t index = 0; index < 80; ++index) assert(samples[index] == 0);
    const auto window = [&samples](std::size_t from, std::size_t length) {
        int result = 0;
        for (std::size_t index = from; index < from + length; ++index) {
            result = (std::max)(result, std::abs(int(samples[80 + index])));
        }
        return result;
    };
    assert(window(0, 50) >= 70 && window(0, 50) <= 84);
    assert(window(575, 50) >= 20 && window(575, 50) <= 30);
    assert(window(1150, 50) >= 70);
    for (std::size_t index = 1360; index < samples.size(); ++index) assert(samples[index] == 0);

    assert(decodeNative({0x27, 0x84, 0x00, 5 | (5 << 3), 60, 12}).type == Apex6TriggerType::Legacy);
    assert(decodeNative({0x27, 0x84, 0x00, 7, 60, 3}).type == Apex6TriggerType::Legacy);
    assert(decodeNative({0x23, 0x84, 0x00, (3 << 3) | 1, 4}).type == Apex6TriggerType::Legacy);
    assert(decodeNative({0x23, 0x84, 0x00, (1 << 3) | 3, 9}).type == Apex6TriggerType::Legacy);
    Apex6TriggerDecodeError error{};
    assert(!decodeApex6TriggerEffect(asb::TriggerSide::Left, {0x27, 0x00, 0x80, 7, 60, 12}, 0, &error));
    assert(error == Apex6TriggerDecodeError::UnsupportedType);

    const auto gallop = decodeNative({0x23, 0x84, 0x00, (1 << 3) | 3, 4});
    assert(gallop.type == Apex6TriggerType::Rhythm && !gallop.rhythm.modulated);
    assert(gallop.rhythm.periodSamples == 250);
    assert(gallop.rhythm.firstPulseOffset == 31 && gallop.rhythm.secondPulseOffset == 93);
    Player galloper{gallop};
    const auto gallopSamples = flatten(galloper.play(hold(120, 125)));
    const auto pulse = apex6ImpactTotalSamples(gallop.rhythm.pulse);
    for (std::size_t index = 0; index < gallopSamples.size(); ++index) {
        const auto cycle = index % 250;
        const bool inFirst = cycle >= 31 && cycle < 31 + pulse;
        const bool inSecond = cycle >= 93 && cycle < 93 + pulse;
        if (!inFirst && !inSecond) assert(gallopSamples[index] == 0);
    }
    int firstPeak = 0;
    int secondPeak = 0;
    for (std::size_t index = 0; index < 250; ++index) {
        if (index >= 31 && index < 31 + pulse) firstPeak = (std::max)(firstPeak, std::abs(int(gallopSamples[index])));
        if (index >= 93 && index < 93 + pulse) secondPeak = (std::max)(secondPeak, std::abs(int(gallopSamples[index])));
    }
    assert(firstPeak >= 55 && firstPeak <= 64 && secondPeak >= 55 && secondPeak <= 64);
}

void testDiagnosticEffectsDecodeAsNativeEffects() {
    using namespace asb::dualsense;
    for (unsigned level = 1; level <= 4; ++level) {
        const auto strength = static_cast<std::uint8_t>(level * 2);
        const auto resistance = decodeNative(
            makeApex6TestTriggerEffect(Apex6TestEffect::Resistance, level));
        assert(resistance.type == Apex6TriggerType::Feedback);
        assert(resistance.startZone == 1 && resistance.endZone == 9);
        assert(resistance.zoneStrengths[0] == 0 && resistance.zoneStrengths[1] == 1);
        assert(resistance.zoneStrengths[9] == strength);
        for (unsigned zone = 2; zone < 10; ++zone) {
            assert(resistance.zoneStrengths[zone] >= resistance.zoneStrengths[zone - 1]);
        }
        const auto weapon = decodeNative(makeApex6TestTriggerEffect(Apex6TestEffect::Weapon, level));
        assert(weapon.type == Apex6TriggerType::Weapon);
        assert(weapon.startZone == 2 && weapon.endZone == 6 && weapon.strength == strength);
        const auto vibration = decodeNative(
            makeApex6TestTriggerEffect(Apex6TestEffect::Vibration, level));
        assert(vibration.type == Apex6TriggerType::Vibration && vibration.frequency == 130);
        assert(vibration.zoneStrengths[0] == 0);
        for (unsigned zone = 1; zone < 10; ++zone) assert(vibration.zoneStrengths[zone] == strength);
        const auto bow = decodeNative(makeApex6TestTriggerEffect(Apex6TestEffect::Bow, level));
        assert(bow.type == Apex6TriggerType::Bow);
        assert(bow.startZone == 1 && bow.endZone == 7 && bow.strength == strength);
        assert(bow.snapStrength == (std::min)(8, strength + 3));
    }
    const auto off = decodeNative(makeApex6TestTriggerEffect(Apex6TestEffect::Off, 2));
    assert(off.type == Apex6TriggerType::Off);

    const std::array<std::pair<Apex6TestEffect, Apex6TriggerType>, 4> kinds{{
        {Apex6TestEffect::Resistance, Apex6TriggerType::Feedback},
        {Apex6TestEffect::Weapon, Apex6TriggerType::Weapon},
        {Apex6TestEffect::Vibration, Apex6TriggerType::Vibration},
        {Apex6TestEffect::Bow, Apex6TriggerType::Bow},
    }};
    for (const auto& [kind, type] : kinds) {
        for (unsigned start = 0; start <= 255; ++start) {
            for (unsigned force = 0; force <= 255; force += 5) {
                Apex6TriggerDecodeError error{};
                const auto decoded = decodeApex6TriggerEffect(
                    asb::TriggerSide::Left,
                    makeApex6TestTriggerEffect(kind, 2, static_cast<std::uint8_t>(start),
                                               static_cast<std::uint8_t>(force)),
                    0, &error);
                assert(decoded && decoded->type == type);
                assert(error == Apex6TriggerDecodeError::None);
            }
        }
        assert(makeApex6TestTriggerEffect(kind, 0) == makeApex6TestTriggerEffect(kind, 1));
        assert(makeApex6TestTriggerEffect(kind, 99) == makeApex6TestTriggerEffect(kind, 4));
    }
}

void testDiagnosticSweepPosition() {
    using namespace asb::dualsense;
    assert(apex6TestSweepPosition(0) == 0);
    int previous = 0;
    for (std::uint64_t time = 0; time < kApex6TestSweepRiseMilliseconds; ++time) {
        const int position = apex6TestSweepPosition(time);
        assert(position >= previous);
        previous = position;
    }
    assert(previous >= 254);
    for (std::uint64_t time = kApex6TestSweepRiseMilliseconds;
         time < kApex6TestSweepRiseMilliseconds + kApex6TestSweepHoldMilliseconds; ++time) {
        assert(apex6TestSweepPosition(time) == 255);
    }
    previous = 255;
    const auto fallStart = kApex6TestSweepRiseMilliseconds + kApex6TestSweepHoldMilliseconds;
    for (std::uint64_t time = fallStart; time < fallStart + kApex6TestSweepFallMilliseconds; ++time) {
        const int position = apex6TestSweepPosition(time);
        assert(position <= previous);
        previous = position;
    }
    assert(previous <= 1);
    for (std::uint64_t time = fallStart + kApex6TestSweepFallMilliseconds;
         time < kApex6TestSweepPeriodMilliseconds; ++time) {
        assert(apex6TestSweepPosition(time) == 0);
    }
    for (std::uint64_t time = 0; time < 3 * kApex6TestSweepPeriodMilliseconds; time += 7) {
        assert(apex6TestSweepPosition(time) ==
               apex6TestSweepPosition(time + kApex6TestSweepPeriodMilliseconds));
    }
}

class FakeApex6Transport final : public asb::platform::HidTransport {
public:
    FakeApex6Transport() {
        info_.vendorId = asb::flydigi::apex6::kVendorId;
        info_.productId = asb::flydigi::apex6::kProductId;
        info_.usagePage = asb::flydigi::apex6::kUsagePage;
        info_.inputReportLength = 33;
        info_.outputReportLength = 33;
    }

    bool isOpen() const noexcept override { return true; }
    const asb::HidDeviceInfo& info() const noexcept override { return info_; }

    bool writeOutputReport(std::span<const std::uint8_t> report,
                           std::string&) override {
        writes.emplace_back(report.begin(), report.end());
        if (report.size() == 33 &&
            report[3] == asb::flydigi::apex6::kCmdGetInfo) {
            std::vector<std::uint8_t> reply(33, 0);
            reply[1] = asb::flydigi::apex6::kMagic0;
            reply[2] = asb::flydigi::apex6::kMagic1;
            reply[3] = asb::flydigi::apex6::kCmdGetInfo;
            reply[4] = 1;
            reply[6] = asb::flydigi::apex6::kDeviceType;
            reply[30] = 0x9F;
            reply[32] = asb::flydigi::apex6::checksum(
                std::span<const std::uint8_t>(reply).subspan(1));
            replies.push_back(std::move(reply));
        }
        return true;
    }

    asb::platform::HidReadStatus readInputReport(
        std::span<std::uint8_t> report,
        std::chrono::milliseconds,
        std::size_t& bytesRead,
        std::string&) override {
        bytesRead = 0;
        if (replies.empty()) return asb::platform::HidReadStatus::Timeout;
        const auto reply = std::move(replies.front());
        replies.pop_front();
        std::copy(reply.begin(), reply.end(), report.begin());
        bytesRead = reply.size();
        return asb::platform::HidReadStatus::Data;
    }

    asb::HidDeviceInfo info_{};
    std::deque<std::vector<std::uint8_t>> replies;
    std::vector<std::vector<std::uint8_t>> writes;
};

} // namespace

void testTriggerDiagnostics() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, true);
    bridge.updateTriggerPositions(200, 200);
    DualSenseFeedback feedback{};
    feedback.enableBits1 = 0x08;
    feedback.leftTriggerEffect = {0x26, 0xFF, 0x03, 0, 0, 0, 0, 0, 0, 90, 0};
    bridge.handle(feedback);
    feedback.leftTriggerEffect[2] = 0x80;
    bridge.handle(feedback);
    bridge.handle(feedback);
    feedback.leftTriggerEffect[0] = 0x7F;
    bridge.handle(feedback);
    auto stats = bridge.stats();
    assert(stats.triggerUnsupported == 3 && stats.triggerMalformed == 2);
    assert(stats.triggerRejectedStops == 1 && stats.triggerStops == 0);
    assert(stats.triggerTraceCount == 3);
    assert(stats.lastActiveLeft && stats.lastActiveLeft->bytes[9] == 90);
    assert(stats.lastRejectedLeft && stats.lastRejectedLeft->bytes[0] == 0x7F);
    assert(stats.triggerTrace[1].status == Apex6TriggerTraceStatus::Malformed);
    assert(stats.triggerTrace[1].position == 200);

    feedback.leftTriggerEffect = {0x26, 0xFF, 0x03, 0, 0, 0, 0, 0, 0, 1, 0};
    for (unsigned frequency = 1; frequency <= 70; ++frequency) {
        feedback.leftTriggerEffect[9] = static_cast<std::uint8_t>(frequency);
        bridge.handle(feedback);
    }
    feedback.leftTriggerEffect = {0x26};
    bridge.handle(feedback);
    stats = bridge.stats();
    assert(stats.triggerTraceCount == 64 && stats.triggerTraceOverwritten == 10);
    assert(stats.triggerTrace[63].status == Apex6TriggerTraceStatus::Off);
    assert(stats.lastActiveLeft->bytes[9] == 70);
    assert(stats.lastRejectedLeft->bytes[0] == 0x7F);
    assert(stats.triggerUnsupported == 3 && stats.triggerStops == 1);

    DualSenseFeedback audio{};
    audio.kind = FeedbackKind::AudioHapticWaveform;
    audio.audioSequence = 1;
    audio.leftHapticSamples.fill(4000);
    audio.rightHapticSamples.fill(-8000);
    bridge.handle(audio);
    audio.audioSequence = 2;
    audio.leftHapticSamples.fill(0);
    audio.rightHapticSamples.fill(0);
    bridge.handle(audio);
    stats = bridge.stats();
    assert(stats.waveformSilentBlocks == 1);
    assert(stats.waveformLeftActiveRms == 4000.0);
    assert(stats.waveformRightActiveRms == 8000.0);
    assert(stats.waveformLeftThresholded == 0 && stats.waveformRightThresholded == 0);
    assert(stats.rawAudioMeasuredBlocks == 0 && stats.rawAudioFrames == 0);
    audio.audioSequence = 3;
    audio.hasRawAudioMeasurements = true;
    audio.rawAudioFrames = 384;
    audio.rawAudioPeaks = {1234, 5678, 10000, 32768};
    audio.rawHapticLeftSumSquares = 384ULL * 10000 * 10000;
    audio.rawHapticRightSumSquares = 384ULL * 32768 * 32768;
    bridge.handle(audio);
    stats = bridge.stats();
    assert(stats.rawAudioMeasuredBlocks == 1 && stats.rawAudioFrames == 384);
    assert(stats.rawAudioPeaks[0] == 1234 && stats.rawAudioPeaks[3] == 32768);
    assert(stats.rawHapticLeftRms == 10000.0 && stats.rawHapticRightRms == 32768.0);
    assert(stats.waveformSilentBlocks == 2);
}

void testPcmGainCurve() {
    using asb::dualsense::scaleApex6Pcm;
    for (int sample = -32768; sample <= 32767; ++sample) {
        const auto pcm = static_cast<std::int16_t>(sample);
        const auto baseline = sample * 127 / (sample >= 0 ? 32767 : 32768);
        assert(scaleApex6Pcm(pcm) == baseline);
        for (const auto gain : {100U, 125U, 150U, 200U}) {
            const auto value = int(scaleApex6Pcm(pcm, gain));
            assert(value >= -127 && value <= 127);
            assert(std::abs(value) >= std::abs(baseline));
            if (sample > -32768) {
                assert(value >= scaleApex6Pcm(static_cast<std::int16_t>(sample - 1), gain));
            }
            assert(sample >= 0 ? value >= 0 : value <= 0);
        }
    }
    assert(scaleApex6Pcm(0, 200) == 0);
    assert(scaleApex6Pcm(32767, 200) == 127);
    assert(scaleApex6Pcm(-32768, 200) == -127);
    assert(scaleApex6Pcm(1000, 200) > scaleApex6Pcm(1000));
    assert(scaleApex6Pcm(10028, 200) == 59); // ZZZ capture peak, not a linear 2x/clipping boost.
    assert(scaleApex6Pcm(1000, 999) == scaleApex6Pcm(1000, 200));
    assert(scaleApex6Pcm(1000, 0) == scaleApex6Pcm(1000));
}

void testApex6EnvelopeThresholdPolicy() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    asb::haptics::HapticConfig config{};
    config.activationThreshold = 0.95; // Shared setting may be high for APEX 4/5.
    Apex6HapticBridge bridge(device, config, true);
    DualSenseFeedback feedback{};
    feedback.kind = FeedbackKind::AudioHaptics;
    feedback.leftPeak = 5000; // Suppressed by the default APEX 4/5 processor.
    bridge.handle(feedback);
    assert(bridge.stats().audioEnvelopeActive == 1);
    // The caller's config and conventional-motor behavior remain unchanged.
    assert(config.activationThreshold == 0.95);
    assert(asb::haptics::HapticProcessor(config).process(feedback).lowFrequency == 0);
}

void testStrength(unsigned strength, double threshold, int pcmAmplitude = 30000,
                  unsigned gain = 100, bool leftOnly = false) {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    asb::haptics::HapticConfig config{};
    config.activationThreshold = threshold;
    Apex6HapticBridge bridge(device, config, true, strength, strength, gain);
    bridge.updateTriggerPositions(200, 200);
    assert(bridge.start(error));
    DualSenseFeedback trigger{};
    trigger.enableBits1 = 0x0C;
    trigger.leftTriggerEffect = {1, 0, 100};
    trigger.rightTriggerEffect = trigger.leftTriggerEffect;
    bridge.handle(trigger);
    DualSenseFeedback waveform{};
    waveform.kind = FeedbackKind::AudioHapticWaveform;
    waveform.leftHapticSamples.fill(static_cast<std::int16_t>(pcmAmplitude));
    waveform.rightHapticSamples.fill(leftOnly ? 0 : static_cast<std::int16_t>(-pcmAmplitude));
    for (unsigned sequence = 1; sequence <= 4; ++sequence) {
        waveform.audioSequence = sequence;
        bridge.handle(waveform);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(bridge.stop(error));
    int maximumGrip = 0;
    int maximumTrigger = 0;
    for (const auto& report : transport->writes) {
        if (report[3] != apex6::kCmdRealtimeMotor) continue;
        for (std::size_t sample = 0; sample < 8; ++sample) {
            maximumGrip = (std::max)(maximumGrip, std::abs(int(report[7 + sample * 3]) - 128));
            maximumGrip = (std::max)(maximumGrip, std::abs(int(report[8 + sample * 3]) - 128));
            if (leftOnly) assert(report[8 + sample * 3] == 128);
            maximumTrigger = (std::max)(maximumTrigger, std::abs(int(report[6 + sample * 3]) - 128));
        }
    }
    if (strength == 0) assert(maximumGrip == 0);
    else assert(maximumGrip == int(scaleApex6Pcm(static_cast<std::int16_t>(pcmAmplitude), gain)) * int(strength) / 100);
    if (strength == 0) assert(maximumTrigger == 0);
    else assert(maximumTrigger > 0 && maximumTrigger <= int(strength));
    const auto stats = bridge.stats();
    if (strength == 0) assert(stats.waveformActiveRendered == 0);
    else assert(stats.waveformActiveRendered > 0);
    assert(stats.waveformLeftThresholded == 0 && stats.waveformRightThresholded == 0);
    assert(stats.pcmGainPercent == gain);
    assert(stats.pcmOutputLeftPeak == static_cast<unsigned>(maximumGrip));
    const auto expectedRight = leftOnly ? 0 :
        std::abs(int(scaleApex6Pcm(static_cast<std::int16_t>(-pcmAmplitude), gain))) * int(strength) / 100;
    assert(stats.pcmOutputRightPeak == static_cast<unsigned>(expectedRight));
}

void testSubQuantizationPcmDoesNotCountRumbleAsRenderedPcm() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    asb::haptics::HapticConfig config{};
    config.activationThreshold = 0;
    Apex6HapticBridge bridge(device, config, true);
    assert(bridge.start(error));
    DualSenseFeedback rumble{};
    rumble.enableBits1 = 0x01;
    rumble.rumbleRight = 100;
    bridge.handle(rumble);
    DualSenseFeedback waveform{};
    waveform.kind = FeedbackKind::AudioHapticWaveform;
    waveform.leftHapticSamples.fill(1); // Nonzero, but below 8-bit output resolution.
    for (unsigned sequence = 1; sequence <= 4; ++sequence) {
        waveform.audioSequence = sequence;
        bridge.handle(waveform);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(bridge.stop(error));
    const auto stats = bridge.stats();
    assert(stats.waveformLeftActiveBlocks == 4 && stats.waveformActiveRendered == 0);
    assert(stats.gripRumbleFrames != 0);
    assert(stats.pcmOutputLeftPeak == 0 && stats.pcmOutputRightPeak == 0);
}

void testCompanionReportsAndSilentAudioKeepGameRumble() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, true);
    assert(bridge.start(error));
    DualSenseFeedback game{};
    game.enableBits1 = 0x03;
    game.rumbleLeft = 180;
    game.rumbleRight = 90;
    bridge.handle(game);
    const auto writesBefore = transport->writes.size();
    DualSenseFeedback companion{};
    companion.leftTriggerEffect = {0x05};
    companion.rightTriggerEffect = {0x05};
    DualSenseFeedback silence{};
    silence.kind = FeedbackKind::AudioHapticWaveform;
    for (unsigned step = 1; step <= 25; ++step) {
        companion.enableBits1 = (step % 2) != 0 ? 0x0E : 0x0C;
        bridge.handle(companion);
        silence.audioSequence = step;
        bridge.handle(silence);
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    assert(bridge.stop(error));
    int leftGrip = 0;
    int rightGrip = 0;
    for (std::size_t index = writesBefore; index < transport->writes.size(); ++index) {
        const auto& report = transport->writes[index];
        if (report[3] != apex6::kCmdRealtimeMotor) continue;
        for (std::size_t sample = 0; sample < 8; ++sample) {
            leftGrip = (std::max)(leftGrip, std::abs(int(report[7 + sample * 3]) - 128));
            rightGrip = (std::max)(rightGrip, std::abs(int(report[8 + sample * 3]) - 128));
        }
    }
    assert(leftGrip > 0 && rightGrip > 0);
    const auto stats = bridge.stats();
    assert(stats.hidReports == 26 && stats.rumbleUpdates == 1);
    assert(stats.rumbleIgnoredReports == 0);
    assert(stats.waveformSilentBlocks == 25);
    assert(stats.gripRumbleFrames >= 5);
    assert(stats.gripRumbleMaskedFrames == 0);
}

void testIgnoredMotorBytesAreCounted() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, true);
    DualSenseFeedback zeroMotors{};
    zeroMotors.enableBits1 = 0x0E;
    bridge.handle(zeroMotors);
    DualSenseFeedback hapticsSelectOnly{};
    hapticsSelectOnly.enableBits1 = 0x02;
    hapticsSelectOnly.rumbleLeft = 200;
    bridge.handle(hapticsSelectOnly);
    auto stats = bridge.stats();
    assert(stats.rumbleUpdates == 0);
    assert(stats.rumbleIgnoredReports == 1);
    assert(stats.lastIgnoredRumbleEnableBits == 0x02);
    DualSenseFeedback compatible = hapticsSelectOnly;
    compatible.enableBits1 = 0x03;
    bridge.handle(compatible);
    stats = bridge.stats();
    assert(stats.rumbleUpdates == 1 && stats.rumbleIgnoredReports == 1);
}

void testPcmOnSameSideCountsMaskedRumble() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, true);
    assert(bridge.start(error));
    DualSenseFeedback rumble{};
    rumble.enableBits1 = 0x01;
    rumble.rumbleLeft = 150;
    bridge.handle(rumble);
    DualSenseFeedback waveform{};
    waveform.kind = FeedbackKind::AudioHapticWaveform;
    waveform.leftHapticSamples.fill(1);
    for (unsigned sequence = 1; sequence <= 4; ++sequence) {
        waveform.audioSequence = sequence;
        bridge.handle(waveform);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(bridge.stop(error));
    const auto stats = bridge.stats();
    assert(stats.waveformLeftActiveBlocks == 4);
    assert(stats.gripRumbleMaskedFrames > 0);
    assert(stats.pcmOutputLeftPeak == 0);
}

void testIdleSkipsFramesAndWakesImmediately() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;

    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, true);
    assert(bridge.start(error));
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    const auto idle = bridge.stats();
    assert(idle.framesWritten < 40); // 100 ticks without idle suppression
    assert(idle.idleFramesSkipped > 30);
    assert(idle.idleKeepalives > 0);

    DualSenseFeedback silent{};
    silent.kind = FeedbackKind::AudioHapticWaveform;
    silent.audioSequence = 1;
    bridge.handle(silent);
    assert(bridge.stats().silentWaveformSkipped == 1);

    const auto framesBefore = bridge.stats().framesWritten;
    DualSenseFeedback active = silent;
    active.audioSequence = 2;
    active.leftHapticSamples.fill(12000);
    bridge.handle(active);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(bridge.stats().framesWritten > framesBefore);
    assert(bridge.stats().waveformActiveRendered > 0);

    DualSenseFeedback rumble{};
    rumble.enableBits1 = 0x01;
    rumble.rumbleLeft = 200;
    bridge.handle(rumble);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(bridge.stats().gripRumbleFrames > 0);
    assert(bridge.stop(error));
    assert(!bridge.failed());
}

void testTriggerReleaseSendsExplicitZeroFrames() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;

    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, true);
    assert(bridge.start(error));

    DualSenseFeedback press{};
    press.enableBits1 = 0x04;
    press.rightTriggerEffect = {0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 150, 0};
    bridge.handle(press);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    DualSenseFeedback release{};
    release.enableBits1 = 0x04;
    bridge.handle(release);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    assert(bridge.stop(error));

    constexpr std::uint8_t kTriggerEnable = 0x04;
    constexpr std::size_t kFlags = 5;
    std::size_t lastActive = transport->writes.size();
    for (std::size_t index = 0; index < transport->writes.size(); ++index) {
        const auto& report = transport->writes[index];
        if (report.size() != 33 || report[3] != apex6::kCmdRealtimeMotor) continue;
        bool nonZeroTrigger = false;
        for (std::size_t sample = 0; sample < 8; ++sample) {
            nonZeroTrigger = nonZeroTrigger || report[kFlags + 1 + sample * 3] != 0x80;
        }
        if ((report[kFlags] & kTriggerEnable) != 0 && nonZeroTrigger) lastActive = index;
    }
    assert(lastActive < transport->writes.size());
    std::size_t zeroFrames = 0;
    for (std::size_t index = lastActive + 1; index < transport->writes.size(); ++index) {
        const auto& report = transport->writes[index];
        if (report.size() != 33 || report[3] != apex6::kCmdRealtimeMotor) continue;
        if ((report[kFlags] & kTriggerEnable) == 0) break;
        ++zeroFrames;
    }
    assert(zeroFrames >= 3);
}

struct TriggerFrame {
    unsigned route = 0;
    bool enabled = false;
    int peak = 0;
};

std::vector<TriggerFrame> triggerFrames(const FakeApex6Transport& transport) {
    using namespace asb::flydigi;
    std::vector<TriggerFrame> frames;
    for (const auto& report : transport.writes) {
        if (report.size() != 33 || report[3] != apex6::kCmdRealtimeMotor) continue;
        TriggerFrame frame;
        frame.enabled = (report[5] & 0x04U) != 0;
        frame.route = report[5] & 0x03U;
        for (std::size_t sample = 0; sample < 8; ++sample) {
            frame.peak = (std::max)(frame.peak, std::abs(int(report[6 + sample * 3]) - 128));
        }
        frames.push_back(frame);
    }
    return frames;
}

void testBridgeSnapPlaysOneBurstPerPress() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, false);
    bridge.updateTriggerPositions(0, 0);
    DualSenseFeedback feedback{};
    feedback.enableBits1 = 0x04;
    feedback.rightTriggerEffect = {0x25, 0x84, 0x00, 7};
    bridge.handle(feedback);
    assert(bridge.start(error));
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    bridge.updateTriggerPositions(0, 255);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    bridge.updateTriggerPositions(0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    bridge.updateTriggerPositions(0, 255);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    assert(bridge.stop(error));
    const auto stats = bridge.stats();
    assert(stats.weaponBreaks == 2 && stats.bowBreaks == 0);
    assert(stats.writeFailures == 0);
    unsigned audible = 0;
    for (const auto& frame : triggerFrames(*transport)) {
        if (frame.peak == 0) continue;
        ++audible;
        assert(frame.enabled && frame.route == static_cast<unsigned>(apex6::TriggerRoute::Right));
        assert(frame.peak <= kApex6TriggerNativeMaximumDrive);
    }
    assert(audible == 2 * (kBurstFrames));
}

void testBridgeDiagnosticPresetPlaysThroughTheRenderer() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, false);
    bridge.updateTriggerPositions(0, 0);
    const auto pistol = makeApex6WeaponPreset(Apex6WeaponPreset::Pistol, 4);
    bridge.setDiagnosticEffect(asb::TriggerSide::Right, pistol);
    assert(bridge.start(error));
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    bridge.updateTriggerPositions(0, 255);
    std::this_thread::sleep_for(std::chrono::milliseconds(320));
    assert(bridge.stop(error));
    assert(bridge.stats().weaponBreaks == 1);
    unsigned audible = 0;
    for (const auto& frame : triggerFrames(*transport)) {
        if (frame.peak == 0) continue;
        ++audible;
        assert(frame.route == static_cast<unsigned>(apex6::TriggerRoute::Right));
        assert(frame.peak <= pistol.impactDrive);
    }
    assert(audible == apex6ImpactFrames(pistol.impact));
}

void testBridgeBurstTakesTheRouteFromASustainedTrigger() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, false);
    bridge.updateTriggerPositions(255, 0);
    DualSenseFeedback feedback{};
    feedback.enableBits1 = 0x0C;
    feedback.leftTriggerEffect = {0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 90, 0};
    feedback.rightTriggerEffect = {0x25, 0x84, 0x00, 7};
    bridge.handle(feedback);
    assert(bridge.start(error));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    bridge.updateTriggerPositions(255, 255);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    assert(bridge.stop(error));
    assert(bridge.stats().weaponBreaks == 1);
    unsigned rightFrames = 0;
    unsigned leftFrames = 0;
    for (const auto& frame : triggerFrames(*transport)) {
        if (frame.peak == 0) continue;
        if (frame.route == static_cast<unsigned>(apex6::TriggerRoute::Right)) ++rightFrames;
        if (frame.route == static_cast<unsigned>(apex6::TriggerRoute::Left)) ++leftFrames;
    }
    assert(rightFrames == kBurstFrames);
    assert(leftFrames > 0);
}

void testShapingHelpers() {
    using namespace asb::dualsense;
    for (int raw = -128; raw <= 127; ++raw) {
        const auto sample = static_cast<std::int8_t>(raw);
        assert(limitApex6TriggerSample(sample, 100) == sample);
        assert(applyApex6GripDuck(sample, 1000) == sample);
        assert(applyApex6GripDuck(sample, 0) == 0);
    }
    for (unsigned ceiling = 20; ceiling < 100; ceiling += 5) {
        const int bound = static_cast<int>(127.0 * ceiling / 100.0 + 0.5);
        const int knee = static_cast<int>(127.0 * ceiling / 200.0);
        int previous = 0;
        for (int magnitude = 0; magnitude <= 127; ++magnitude) {
            const int positive = limitApex6TriggerSample(static_cast<std::int8_t>(magnitude), ceiling);
            const int negative = limitApex6TriggerSample(static_cast<std::int8_t>(-magnitude), ceiling);
            assert(positive == -negative);
            assert(positive >= previous && positive <= bound);
            if (magnitude <= knee) assert(positive == magnitude);
            previous = positive;
        }
        assert(std::abs(int(limitApex6TriggerSample(-128, ceiling))) <= bound);
    }
    assert(limitApex6TriggerSample(100, 5) == limitApex6TriggerSample(100, 20));
    assert(limitApex6TriggerSample(100, 999) == 100);

    assert(apex6GripDuckGainPermille(255, 255, 0) == 1000);
    assert(apex6GripDuckGainPermille(0, 0, 100) == 1000);
    assert(apex6GripDuckGainPermille(20, 20, 100) == 1000);
    assert(apex6GripDuckGainPermille(100, 0, 100) == 0);
    assert(apex6GripDuckGainPermille(0, 255, 100) == 0);
    assert(apex6GripDuckGainPermille(255, 255, 50) == 500);
    assert(apex6GripDuckGainPermille(60, 0, 100) == 500);
    assert(apex6GripDuckGainPermille(255, 255, 500) == 0);
    unsigned previousGain = 1000;
    for (unsigned position = 0; position <= 255; ++position) {
        const auto gain = apex6GripDuckGainPermille(
            static_cast<std::uint8_t>(position), 0, 80);
        assert(gain <= previousGain);
        previousGain = gain;
    }
    assert(applyApex6GripDuck(127, 500) == 63);
    assert(applyApex6GripDuck(-127, 500) == -63);
}

struct ShapingRun {
    int maximumGrip = 0;
    int maximumTrigger = 0;
    asb::dualsense::Apex6HapticBridgeStats stats;
};

ShapingRun runShaping(unsigned duck, unsigned ceiling, std::uint8_t position) {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, true, 100, 100, 100, duck, ceiling);
    bridge.updateTriggerPositions(position, position);
    assert(bridge.start(error));
    DualSenseFeedback trigger{};
    trigger.enableBits1 = 0x0C;
    trigger.leftTriggerEffect = {1, 0, 100};
    trigger.rightTriggerEffect = trigger.leftTriggerEffect;
    bridge.handle(trigger);
    DualSenseFeedback waveform{};
    waveform.kind = FeedbackKind::AudioHapticWaveform;
    waveform.leftHapticSamples.fill(30000);
    waveform.rightHapticSamples.fill(-30000);
    for (unsigned sequence = 1; sequence <= 4; ++sequence) {
        waveform.audioSequence = sequence;
        bridge.handle(waveform);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(bridge.stop(error));
    ShapingRun run;
    for (const auto& report : transport->writes) {
        if (report[3] != apex6::kCmdRealtimeMotor) continue;
        for (std::size_t sample = 0; sample < 8; ++sample) {
            run.maximumGrip = (std::max)(run.maximumGrip, std::abs(int(report[7 + sample * 3]) - 128));
            run.maximumGrip = (std::max)(run.maximumGrip, std::abs(int(report[8 + sample * 3]) - 128));
            run.maximumTrigger = (std::max)(run.maximumTrigger, std::abs(int(report[6 + sample * 3]) - 128));
        }
    }
    run.stats = bridge.stats();
    return run;
}

void testShapingBridge() {
    const auto baseline = runShaping(0, 100, 200);
    assert(baseline.maximumGrip > 0 && baseline.maximumTrigger > 0);
    assert(baseline.stats.gripDuckPercent == 0 && baseline.stats.triggerCeilingPercent == 100);
    assert(baseline.stats.gripDuckedBlocks == 0 && baseline.stats.triggerLimitedSamples == 0);

    const auto pressed = runShaping(100, 100, 200);
    assert(pressed.maximumGrip == 0);
    assert(pressed.maximumTrigger == baseline.maximumTrigger);
    assert(pressed.stats.gripDuckPercent == 100 && pressed.stats.gripDuckedBlocks > 0);

    const auto released = runShaping(100, 100, 0);
    assert(released.maximumGrip == baseline.maximumGrip);
    assert(released.stats.gripDuckedBlocks == 0);

    const auto half = runShaping(50, 100, 200);
    assert(half.maximumGrip == baseline.maximumGrip * 500 / 1000);

    const auto limited = runShaping(0, 50, 200);
    assert(limited.maximumGrip == baseline.maximumGrip);
    assert(limited.maximumTrigger > 0 && limited.maximumTrigger <= 64);
    assert(limited.maximumTrigger <= baseline.maximumTrigger);
    if (baseline.maximumTrigger > 32) assert(limited.stats.triggerLimitedSamples > 0);
    assert(limited.stats.triggerCeilingPercent == 50);

    const auto clamped = runShaping(500, 5, 0);
    assert(clamped.stats.gripDuckPercent == 100 && clamped.stats.triggerCeilingPercent == 20);
}

std::vector<std::vector<std::uint8_t>> realtimeReports(const FakeApex6Transport& transport) {
    std::vector<std::vector<std::uint8_t>> result;
    for (const auto& report : transport.writes) {
        if (report.size() == 33 && report[3] == asb::flydigi::apex6::kCmdRealtimeMotor) {
            result.push_back(report);
        }
    }
    return result;
}

int decodedSample(const std::vector<std::uint8_t>& report, std::size_t sample, std::size_t channel) {
    return static_cast<int>(report[6 + sample * 3 + channel]) - 128;
}

bool reportIsSilent(const std::vector<std::uint8_t>& report) {
    for (std::size_t sample = 0; sample < 8; ++sample) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            if (decodedSample(report, sample, channel) != 0) return false;
        }
    }
    return true;
}

void testApex6TwoKilohertzNativePcm() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, true, 100, 100, 100, 0, 100, 2000);
    assert(bridge.start(error));
    const auto leftSample = [](std::size_t index) {
        return static_cast<std::int16_t>(2000 + index * 1800);
    };
    DualSenseFeedback waveform{};
    waveform.kind = FeedbackKind::AudioHapticWaveform2k;
    for (std::size_t index = 0; index < 16; ++index) {
        waveform.leftHapticSamples2k[index] = leftSample(index);
        waveform.rightHapticSamples2k[index] = static_cast<std::int16_t>(-leftSample(index));
    }
    for (unsigned sequence = 1; sequence <= 6; ++sequence) {
        waveform.audioSequence = sequence;
        bridge.handle(waveform);
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(bridge.stop(error));

    const auto reports = realtimeReports(*transport);
    assert(reports.size() > 3 + 4);
    const auto streamed = reports.size() - 3;
    for (std::size_t index = 0; index < streamed; ++index) {
        assert((reports[index][5] & 0x80) == 0);
        assert(reports[index][5] == 0x1B);
    }
    for (std::size_t index = 0; index < 4; ++index) assert(reportIsSilent(reports[index]));
    assert((reports[streamed][5] & 0x80) != 0);

    const auto expected = [&](std::size_t index) {
        return static_cast<int>(scaleApex6Pcm(leftSample(index), 100));
    };
    bool foundBlock = false;
    for (std::size_t index = 0; index + 1 < streamed && !foundBlock; ++index) {
        bool match = true;
        for (std::size_t sample = 0; sample < 8 && match; ++sample) {
            match = decodedSample(reports[index], sample, 1) == expected(sample) &&
                    decodedSample(reports[index], sample, 2) == -expected(sample) &&
                    decodedSample(reports[index + 1], sample, 1) == expected(sample + 8) &&
                    decodedSample(reports[index + 1], sample, 2) == -expected(sample + 8);
        }
        foundBlock = match;
    }
    assert(foundBlock);

    const auto stats = bridge.stats();
    assert(stats.sampleRateHz == 2000);
    assert(stats.waveform2kBlocks == 6);
    assert(stats.waveformUpsampledBlocks == 0);
    assert(stats.primeReports == 4);
    assert(stats.framesWritten >= reports.size() - 3);
}

void testApex6TwoKilohertzUpsamplesNarrowBlocks() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, true, 100, 100, 100, 0, 100, 2000);
    assert(bridge.start(error));
    DualSenseFeedback waveform{};
    waveform.kind = FeedbackKind::AudioHapticWaveform;
    waveform.leftHapticSamples.fill(12000);
    waveform.rightHapticSamples.fill(-12000);
    for (unsigned sequence = 1; sequence <= 5; ++sequence) {
        waveform.audioSequence = sequence;
        bridge.handle(waveform);
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(bridge.stop(error));

    const auto reports = realtimeReports(*transport);
    assert(reports.size() > 3);
    const auto streamed = reports.size() - 3;
    std::size_t first = streamed;
    for (std::size_t index = 0; index < streamed; ++index) {
        assert((reports[index][5] & 0x80) == 0);
        if (first == streamed && !reportIsSilent(reports[index])) first = index;
    }
    assert(first + 1 < streamed);
    const int full = static_cast<int>(scaleApex6Pcm(12000, 100));
    const int half = static_cast<int>(scaleApex6Pcm(6000, 100));
    assert(decodedSample(reports[first], 0, 1) == half);
    for (std::size_t sample = 1; sample < 8; ++sample) {
        assert(decodedSample(reports[first], sample, 1) == full);
        assert(decodedSample(reports[first], sample, 2) == -full);
    }
    for (std::size_t sample = 0; sample < 8; ++sample) {
        assert(decodedSample(reports[first + 1], sample, 1) == full);
    }
    const auto stats = bridge.stats();
    assert(stats.waveformUpsampledBlocks >= 1);
    assert(stats.waveform2kBlocks == 0);
}

void testApex6OneKilohertzIgnoresWideBlocksAndKeepsInterpolation() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    Apex6HapticBridge bridge(device, {}, true);
    assert(bridge.start(error));
    DualSenseFeedback wide{};
    wide.kind = FeedbackKind::AudioHapticWaveform2k;
    wide.leftHapticSamples2k.fill(20000);
    DualSenseFeedback narrow{};
    narrow.kind = FeedbackKind::AudioHapticWaveform;
    narrow.leftHapticSamples.fill(20000);
    for (unsigned sequence = 1; sequence <= 4; ++sequence) {
        wide.audioSequence = sequence;
        narrow.audioSequence = sequence;
        bridge.handle(wide);
        bridge.handle(narrow);
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    assert(bridge.stop(error));
    const auto reports = realtimeReports(*transport);
    assert(reports.size() > 3);
    for (std::size_t index = 0; index + 3 < reports.size(); ++index) {
        assert((reports[index][5] & 0x80) != 0);
    }
    const auto stats = bridge.stats();
    assert(stats.sampleRateHz == 1000);
    assert(stats.waveformBlocks == 4);
    assert(stats.waveform2kBlocks == 0 && stats.primeReports == 0);
    assert(stats.waveformUpsampledBlocks == 0);
}

void testApex6SampleRateIsSanitised() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    Apex6HapticBridge odd(device, {}, true, 100, 100, 100, 0, 100, 1500);
    assert(odd.stats().sampleRateHz == 1000);
    Apex6HapticBridge fast(device, {}, true, 100, 100, 100, 0, 100, 2000);
    assert(fast.stats().sampleRateHz == 2000);
    Apex6HapticBridge normal(device, {}, true);
    assert(normal.stats().sampleRateHz == 1000);
}

int main() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;

    testApex6SampleRateIsSanitised();
    testApex6TwoKilohertzNativePcm();
    testApex6TwoKilohertzUpsamplesNarrowBlocks();
    testApex6OneKilohertzIgnoresWideBlocksAndKeepsInterpolation();
    testShapingHelpers();
    testShapingBridge();
    testTriggerReleaseSendsExplicitZeroFrames();
    testIdleSkipsFramesAndWakesImmediately();
    testNativeTriggerEffects();
    testPcmGainCurve();
    testApex6EnvelopeThresholdPolicy();
    testBowEffect();
    testContinuousCarrierClock();
    testSnapIsOneShotBurst();
    testLegacyBreakAndLockAreBursts();
    testLegacyContinuousModesStartSmoothly();
    testProgressiveFeedbackFollowsProfile();
    testVibrationHonoursZoneStrengthsAndFrequency();
    testBowRampsThenBurstsThenStaysSilent();
    testRetargetKeepsBurstWithoutRetrigger();
    testRendererBoundsAndContinuity();
    testPresetTable();
    testPositionOneShotPlaysOncePerPress();
    testPresetsFollowTheSceneShapes();
    testSniperTailDecaysForAboutASecond();
    testRhythmTrainFollowsTheSceneCadence();
    testMachineAndGallopingDecodeToRhythm();
    testDiagnosticEffectsDecodeAsNativeEffects();
    testDiagnosticSweepPosition();
    testBridgeDiagnosticPresetPlaysThroughTheRenderer();
    testBridgeSnapPlaysOneBurstPerPress();
    testBridgeBurstTakesTheRouteFromASustainedTrigger();
    testTriggerDiagnostics();
    testSubQuantizationPcmDoesNotCountRumbleAsRenderedPcm();
    testCompanionReportsAndSilentAudioKeepGameRumble();
    testIgnoredMotorBytesAreCounted();
    testPcmOnSameSideCountsMaskedRumble();
    testStrength(100, 0.0);
    testStrength(50, 0.0);
    testStrength(0, 0.0);
    testStrength(100, 0.95);
    testStrength(100, 0.12, 1000); // Quiet native texture must survive the default threshold.
    testStrength(100, 0.95, 1000);
    testStrength(100, 0.12, 10028, 200);
    testStrength(50, 0.12, 1000, 150, true);
    testStrength(0, 0.12, 10028, 200);
    testStrength(100, 0.12, 32767, 200);

    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    assert(device.identity() && device.identity()->isApex6());

    Apex6HapticBridge bridge(device, {}, true);
    bridge.updateTriggerPositions(200, 0);

    DualSenseFeedback trigger{};
    trigger.enableBits1 = 0x08;
    trigger.leftTriggerEffect[0] = 0x21;
    trigger.leftTriggerEffect[1] = 0xFC; // active from zone 2 to the end of travel
    trigger.leftTriggerEffect[2] = 0x03;
    trigger.leftTriggerEffect[3] = 60;
    bridge.handle(trigger);

    DualSenseFeedback waveform{};
    waveform.kind = FeedbackKind::AudioHapticWaveform;
    waveform.audioSequence = 1;
    waveform.leftHapticSamples.fill(12000);
    waveform.rightHapticSamples.fill(-12000);
    assert(bridge.start(error));
    bridge.handle(waveform);
    bridge.handle(waveform); // duplicate sequence 1
    waveform.audioSequence = 0;
    bridge.handle(waveform); // older than sequence 1
    waveform.audioSequence = 3; // sequence 2 was lost upstream
    bridge.handle(waveform);
    waveform.audioSequence = 4;
    bridge.handle(waveform);
    waveform.audioSequence = 5; // bounded queue evicts sequence 1
    bridge.handle(waveform);

    std::this_thread::sleep_for(std::chrono::milliseconds(42));
    DualSenseFeedback rumble{};
    rumble.enableBits1 = 0x01;
    rumble.rumbleLeft = 120;
    bridge.handle(rumble);
    waveform.audioSequence = 6;
    waveform.leftHapticSamples.fill(0);
    waveform.rightHapticSamples.fill(0);
    bridge.handle(waveform);
    std::this_thread::sleep_for(std::chrono::milliseconds(24));
    trigger.leftTriggerEffect[0] = 0x05;
    bridge.handle(trigger);
    bridge.updateTriggerPositions(200, 200);
    DualSenseFeedback both{};
    both.enableBits1 = 0x0C;
    both.leftTriggerEffect[0] = both.rightTriggerEffect[0] = 0x26;
    both.leftTriggerEffect[1] = both.rightTriggerEffect[1] = 0xFC;
    both.leftTriggerEffect[2] = both.rightTriggerEffect[2] = 0x03;
    both.leftTriggerEffect[9] = both.rightTriggerEffect[9] = 90;
    bridge.handle(both);
    std::this_thread::sleep_for(std::chrono::milliseconds(24));
    both.enableBits1 = 0x04;
    both.rightTriggerEffect[9] = 140;
    bridge.handle(both);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(bridge.stop(error));
    assert(!bridge.failed());
    waveform.audioSequence = 7;
    waveform.leftHapticSamples.fill(12000);
    bridge.handle(waveform); // stale by the time the next enable delay completes
    const auto restartWriteStart = transport->writes.size();
    assert(bridge.start(error));
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
    assert(bridge.stop(error));
    for (std::size_t index = restartWriteStart; index < transport->writes.size(); ++index) {
        const auto& report = transport->writes[index];
        if (report[3] == apex6::kCmdRealtimeMotor) {
            assert((report[5] & 0x04U) == 0); // no previous trigger effect survives restart
        }
    }

    bool foundRealtime = false;
    bool foundWaveform = false;
    bool foundLeftTrigger = false;
    bool foundBipolarTrigger = false;
    for (const auto& report : transport->writes) {
        assert(report.size() == apex6::kReportSize);
        assert(report[0] == apex6::kReportId);
        assert(report[3] != kCmdSetRumble);
        assert(report[3] != kCmdSetForceTrigger);
        if (report[3] != apex6::kCmdRealtimeMotor) continue;
        foundRealtime = true;
        if (report[7] != 0x80 || report[8] != 0x80) foundWaveform = true;
        if ((report[5] & 0x07) == 0x04) {
            bool positive = false;
            bool negative = false;
            for (std::size_t sample = 0; sample < 8; ++sample) {
                const auto encoded = report[6 + sample * 3];
                positive = positive || encoded > 0x80;
                negative = negative || encoded < 0x80;
            }
            foundLeftTrigger = foundLeftTrigger || positive || negative;
            foundBipolarTrigger = foundBipolarTrigger || (positive && negative);
        }
    }
    assert(foundRealtime);
    assert(foundWaveform);
    assert(foundLeftTrigger);
    assert(foundBipolarTrigger);
    const auto stats = bridge.stats();
    assert(stats.framesWritten >= 2);
    assert(stats.waveformBlocks == 8);
    assert(stats.waveformBlocksRendered == 4);
    assert(stats.waveformBlocksDropped == 4);
    assert(stats.waveformQueueMaxDepth == 3);
    assert(stats.waveformSequenceGaps == 1);
    assert(stats.waveformDuplicates == 1);
    assert(stats.waveformOutOfOrder == 1);
    assert(stats.waveformOverflowDrops == 1);
    assert(stats.waveformStaleDrops == 1);
    assert(stats.hidReports == 5);
    assert(stats.triggerLeftUpdates == 3);
    assert(stats.triggerRightUpdates == 2);
    assert(stats.triggerActiveUpdates == 4);
    assert(stats.triggerStops == 1);
    assert(stats.lastLeftTriggerType == 0x26);
    assert(stats.lastRightTriggerType == 0x26);
    assert(stats.bothTriggerFrames > 0);
    assert(stats.rightTriggerFrames > 0);
    assert(stats.waveformLeftActiveBlocks == 7);
    assert(stats.waveformRightActiveBlocks == 6);
    assert(stats.waveformLeftPeak == 12000);
    assert(stats.waveformRightPeak == 12000);
    assert(stats.waveformActiveRendered == 3);
    assert(stats.gripRumbleFrames > 0); // silent PCM must not mask HID rumble
    assert(stats.hapticEnables == 2 && stats.hapticDisables == 2);
    assert(stats.writeFailures == 0);
    return 0;
}
