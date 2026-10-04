#ifdef NDEBUG
#undef NDEBUG
#endif

#include "dualsense/Apex6HapticBridge.h"
#include "dualsense/Apex6TriggerEffect.h"
#include "flydigi/Apex6Protocol.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

int peak(const std::array<std::int8_t, 8>& samples) {
    int result = 0;
    for (const auto sample : samples) result = (std::max)(result, std::abs(static_cast<int>(sample)));
    return result;
}

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
    assert(peak(renderApex6TriggerEffect(*weapon, 100, weaponState)) > 0);
    assert(peak(renderApex6TriggerEffect(*weapon, 200, weaponState)) > 0);
    assert(peak(renderApex6TriggerEffect(*weapon, 200, weaponState)) > 0);
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
        assert(peak(renderApex6TriggerEffect(*bow, 220, state)) > 0);
        assert(peak(renderApex6TriggerEffect(*bow, 220, state)) > 0);
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
                const auto lt = renderApex6TriggerEffect(effect, 255, left, block * 8 + 8);
                const auto rt = renderApex6TriggerEffect(effect, 255, right, block * 8 + 8);
                assert(lt == rt); // Identical effects remain eligible for Both, at full duty.
            }
        }
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

void testStrength(unsigned strength, double threshold, int pcmAmplitude = 30000) {
    using namespace asb::dualsense;
    using namespace asb::flydigi;
    auto* transport = new FakeApex6Transport();
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    asb::haptics::HapticConfig config{};
    config.activationThreshold = threshold;
    Apex6HapticBridge bridge(device, config, true, strength, strength);
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
    waveform.rightHapticSamples.fill(static_cast<std::int16_t>(-pcmAmplitude));
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
            maximumTrigger = (std::max)(maximumTrigger, std::abs(int(report[6 + sample * 3]) - 128));
        }
    }
    if (strength == 0) assert(maximumGrip == 0);
    else assert(maximumGrip == (pcmAmplitude * 127 / 32767) * int(strength) / 100);
    if (strength == 0) assert(maximumTrigger == 0);
    else assert(maximumTrigger > 0 && maximumTrigger <= int(strength));
    const auto stats = bridge.stats();
    if (strength == 0) assert(stats.waveformActiveRendered == 0);
    else assert(stats.waveformActiveRendered > 0);
    assert(stats.waveformLeftThresholded == 0 && stats.waveformRightThresholded == 0);
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
}

int main() {
    using namespace asb::dualsense;
    using namespace asb::flydigi;

    testNativeTriggerEffects();
    testBowEffect();
    testContinuousCarrierClock();
    testTriggerDiagnostics();
    testSubQuantizationPcmDoesNotCountRumbleAsRenderedPcm();
    testStrength(100, 0.0);
    testStrength(50, 0.0);
    testStrength(0, 0.0);
    testStrength(100, 0.95);
    testStrength(100, 0.12, 1000); // Quiet native texture must survive the default threshold.
    testStrength(100, 0.95, 1000);

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
