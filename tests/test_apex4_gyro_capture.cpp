#ifdef NDEBUG
#undef NDEBUG
#endif

#include "diagnostics/Apex4GyroCapture.h"

#include <array>
#include <cassert>
#include <string>
#include <vector>

int main() {
    using namespace asb::diagnostics;

    Apex4MotionPhaseCapture phase("yaw", "rotate", 2);
    std::array<std::uint8_t, 32> first{};
    first[0] = 0x04;
    first[1] = 0xFE;
    first[2] = 0x10;
    first[9] = 0x00;
    first[15] = 0x20; // Known accelerometer Z byte.
    assert(phase.addReport(first, 100));

    auto second = first;
    second[2] = 0x20; // Unknown byte retained for discovery.
    second[9] = 0x01; // Known button byte.
    assert(phase.addReport(second, 200));

    auto third = second;
    third[2] = 0x30;
    assert(phase.addReport(third, 300));
    assert(phase.samples().size() == 2);
    assert(phase.droppedSamples() == 1);
    assert(phase.motionDataObserved());

    std::array<std::uint8_t, 32> unrelated{};
    unrelated[0] = 0x04;
    unrelated[1] = 0xEC;
    assert(!phase.addReport(unrelated, 400));
    assert(phase.rejectedReports() == 1);

    std::array<std::uint8_t, 8> shortState{};
    shortState[0] = 0x04;
    shortState[1] = 0xFE;
    assert(!phase.addReport(shortState, 500));
    assert(phase.shortReports() == 1);

    const auto activity = phase.byteActivity();
    assert(activity.size() == 2);
    assert(activity[0].offset == 2);
    assert(activity[0].minimum == 0x10);
    assert(activity[0].maximum == 0x30);
    assert(activity[0].changes == 2);
    assert(activity[0].uniqueValues == 3);
    assert(!activity[0].knownControl);
    assert(activity[1].offset == 9);
    assert(activity[1].knownControl);
    assert(isKnownApex4ControlOffset(24));
    assert(!isKnownApex4ControlOffset(25));
    assert(isKnownApex4MotionOffset(4));
    assert(isKnownApex4MotionOffset(18));
    assert(isKnownApex4MotionOffset(20));
    assert(isKnownApex4MotionOffset(26));
    assert(isKnownApex4MotionOffset(27));
    assert(!isKnownApex4MotionOffset(19));
    assert(isKnownApex4MotionOffset(30));
    assert(!isKnownApex4MotionOffset(28));

    Apex4MotionPhaseCapture nativeOnly("pitch", "tilt", 2);
    std::array<std::uint8_t, 32> nativeReport{};
    nativeReport[0] = 4;
    nativeReport[1] = 0xFE;
    assert(nativeOnly.addReport(nativeReport, 0));
    assert(!nativeOnly.motionDataObserved());
    nativeReport[26] = 1;
    assert(nativeOnly.addReport(nativeReport, 100));
    assert(nativeOnly.motionDataObserved());

    Apex4GyroCaptureMetadata metadata{};
    metadata.model = "APEX 4";
    metadata.connection = "dongle";
    metadata.connectionRaw = 2;
    metadata.vendorId = 0x04B4;
    metadata.productId = 0x2412;
    metadata.declaredInputReportLength = 32;
    metadata.phaseSeconds = 5;
    const std::vector<Apex4MotionPhaseCapture> phases{phase};
    const auto json = formatApex4GyroCaptureJson(metadata, phases);
    assert(json.find("\"tool\": \"ApexSenseBridge apex4-gyro-capture\"") !=
           std::string::npos);
    assert(json.find("\"offset\": 2") != std::string::npos);
    assert(json.find("\"motion_data_observed\": true") != std::string::npos);
    assert(json.find("0410") == std::string::npos);
    assert(json.find("04FE10") != std::string::npos);
    assert(json.find("keyboard") != std::string::npos);

    return 0;
}
