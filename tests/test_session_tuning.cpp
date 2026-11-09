#ifdef NDEBUG
#undef NDEBUG
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/SessionTuning.h"

#include <atomic>
#include <cassert>
#include <string>
#include <string_view>

namespace {

using namespace asb::platform;

std::wstring widen(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

void writeRequest(SessionTuningBlock* block, const SessionTuningRequest& request) {
    std::atomic_ref<std::uint32_t> sequence(block->request.sequence);
    std::uint32_t expected = sequence.load();
    while ((expected & 1U) != 0U ||
           !sequence.compare_exchange_weak(expected, expected + 1U)) {
        if ((expected & 1U) != 0U) expected = sequence.load();
    }
    block->request.triggerStrengthPercent = request.triggerStrengthPercent;
    block->request.vibrationStrengthPercent = request.vibrationStrengthPercent;
    block->request.gripMute = request.gripMute;
    block->request.triggerEffectsOff = request.triggerEffectsOff;
    block->request.touchpadProfile = request.touchpadProfile;
    block->request.apexProfileSlot = request.apexProfileSlot;
    sequence.store(expected + 2U);
}

void testInterpretation() {
    SessionTuningLimits limits;
    limits.maxVibrationPercent = 100;
    limits.maxTouchpadProfile = 7;
    limits.maxApexSlot = 3;

    SessionTuningRequest untouched;
    assert(!interpretSessionTuningRequest(untouched, limits).any());

    SessionTuningRequest valid;
    valid.triggerStrengthPercent = 40;
    valid.vibrationStrengthPercent = 100;
    valid.gripMute = 1;
    valid.triggerEffectsOff = 0;
    valid.touchpadProfile = 7;
    valid.apexProfileSlot = 3;
    const auto accepted = interpretSessionTuningRequest(valid, limits);
    assert(accepted.triggerStrengthPercent == 40U);
    assert(accepted.vibrationStrengthPercent == 100U);
    assert(accepted.gripMute == true);
    assert(accepted.triggerEffectsOff == false);
    assert(accepted.touchpadProfile == 7U);
    assert(accepted.apexProfileSlot == 3U);

    SessionTuningRequest outOfRange;
    outOfRange.triggerStrengthPercent = 101;
    outOfRange.vibrationStrengthPercent = 101;
    outOfRange.gripMute = 2;
    outOfRange.triggerEffectsOff = 7;
    outOfRange.touchpadProfile = 8;
    outOfRange.apexProfileSlot = 4;
    assert(!interpretSessionTuningRequest(outOfRange, limits).any());

    limits.maxVibrationPercent = 200;
    SessionTuningRequest wide;
    wide.vibrationStrengthPercent = 200;
    assert(interpretSessionTuningRequest(wide, limits).vibrationStrengthPercent == 200U);
}

void testNaming() {
    constexpr std::string_view token = "0123456789abcdef0123456789abcdef";
    assert(sessionTuningMappingName(token) ==
           "Local\\ApexSenseBridge.Session.0123456789abcdef0123456789abcdef.Tuning");
}

void testRoundTrip() {
    constexpr std::string_view token = "fedcba9876543210fedcba9876543210";
    std::string error;
    assert(createSessionTuning("short", error) == nullptr);
    assert(!error.empty());

    error.clear();
    auto tuning = createSessionTuning(token, error);
    assert(tuning && error.empty());
    assert(createSessionTuning(token, error) == nullptr);

    const auto name = widen(sessionTuningMappingName(token));
    HANDLE clientMapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name.c_str());
    assert(clientMapping);
    auto* block = static_cast<SessionTuningBlock*>(MapViewOfFile(
        clientMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, kSessionTuningBlockSize));
    assert(block);
    assert(block->magic == kSessionTuningMagic);
    assert(block->version == kSessionTuningVersion);

    SessionTuningRequest polled;
    assert(!tuning->poll(polled));

    SessionTuningRequest first;
    first.vibrationStrengthPercent = 55;
    first.gripMute = 1;
    writeRequest(block, first);
    assert(tuning->poll(polled));
    assert(polled.sequence == 2U);
    assert(polled.vibrationStrengthPercent == 55U);
    assert(polled.gripMute == 1U);
    assert(polled.triggerStrengthPercent == kSessionTuningKeep);
    assert(!tuning->poll(polled));

    std::atomic_ref<std::uint32_t>(block->request.sequence).store(3U);
    assert(!tuning->poll(polled));
    std::atomic_ref<std::uint32_t>(block->request.sequence).store(4U);
    assert(tuning->poll(polled));
    assert(polled.sequence == 4U);

    SessionTuningRequest second;
    second.triggerStrengthPercent = 10;
    writeRequest(block, second);
    assert(tuning->poll(polled));
    assert(polled.sequence == 6U);
    assert(polled.triggerStrengthPercent == 10U);

    SessionTuningApplied applied;
    applied.requestSequence = polled.sequence;
    applied.triggerStrengthPercent = 10;
    applied.vibrationStrengthPercent = 55;
    applied.gripMute = 1;
    applied.touchpadProfile = 2;
    applied.apexProfileSlot = 1;
    applied.sampleRateHz = 2000;
    applied.controllerModel = 6;
    tuning->publishApplied(applied);
    const auto firstSequence = block->applied.sequence;
    assert((firstSequence & 1U) == 0U);
    assert(block->applied.requestSequence == 6U);
    assert(block->applied.triggerStrengthPercent == 10U);
    assert(block->applied.vibrationStrengthPercent == 55U);
    assert(block->applied.gripMute == 1U);
    assert(block->applied.touchpadProfile == 2U);
    assert(block->applied.apexProfileSlot == 1U);
    assert(block->applied.sampleRateHz == 2000U);
    assert(block->applied.controllerModel == 6U);

    tuning->publishApplied(applied);
    assert(block->applied.sequence == firstSequence + 2U);

    UnmapViewOfFile(block);
    CloseHandle(clientMapping);
    tuning.reset();

    HANDLE gone = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    assert(gone == nullptr);
}

} // namespace

int main() {
    testNaming();
    testInterpretation();
    testRoundTrip();
    return 0;
}
