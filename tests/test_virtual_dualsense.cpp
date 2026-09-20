#ifdef NDEBUG
#undef NDEBUG
#endif

#include "dualsense/DualSenseDescriptor.h"
#include "dualsense/DualSenseFeatureReports.h"
#include "dualsense/DualSenseFeedback.h"
#include "dualsense/DualSenseFirmware.h"
#include "dualsense/DualSenseInput.h"
#include "dualsense/VirtualDualSenseStartup.h"
#include "dualsense/ViiperProtocol.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeVirtualDualSense final : public asb::dualsense::VirtualDualSense {
public:
    bool open(std::string& error, FeedbackHandler handler) override {
        ++openCalls;
        handler_ = std::move(handler);
        const bool succeeds = openCalls > openFailures;
        connected_ = succeeds;
        if (!succeeds) error = "synthetic open failure";
        return succeeds;
    }

    void close() noexcept override {
        ++closeCalls;
        connected_ = false;
        handler_ = {};
    }

    bool updateInput(const asb::dualsense::DualSenseInputState& state,
                     std::string& error) override {
        ++updateCalls;
        lastInput = state;
        if (updateFails) {
            error = "synthetic input failure";
            return false;
        }
        return connected_;
    }

    [[nodiscard]] bool connected() const noexcept override { return connected_; }

    asb::dualsense::VirtualDualSenseStats stats() const override {
        asb::dualsense::VirtualDualSenseStats result{};
        result.connected = connected_;
        return result;
    }

    std::size_t openFailures = 0;
    bool updateFails = false;
    std::size_t openCalls = 0;
    std::size_t closeCalls = 0;
    std::size_t updateCalls = 0;
    asb::dualsense::DualSenseInputState lastInput{};

private:
    bool connected_ = false;
    FeedbackHandler handler_;
};

void testVerifiedStartupRetriesReadinessFailure() {
    using namespace asb::dualsense;

    FakeVirtualDualSense backend;
    DualSenseInputState initial{};
    initial.lx = 0x31;
    initial.r2 = 0x72;
    std::size_t probeCalls = 0;
    std::size_t removalWaits = 0;
    VirtualDualSenseStartupResult result{};
    std::string error;

    const bool started = startVerifiedVirtualDualSense(
        backend, initial, {},
        [&probeCalls](std::string& probeError)
            -> std::optional<DualSenseFirmwareInfo> {
            ++probeCalls;
            if (probeCalls == 1) {
                probeError = "synthetic HID publication timeout";
                return std::nullopt;
            }
            DualSenseFirmwareInfo firmware{};
            firmware.updateVersion = 0x0630;
            return firmware;
        },
        [&removalWaits](std::string&) {
            ++removalWaits;
            return true;
        },
        2, result, error);

    assert(started);
    assert(error.empty());
    assert(result.failure == VirtualDualSenseStartupFailure::None);
    assert(result.attempts == 2);
    assert(result.firmware && result.firmware->updateVersion == 0x0630);
    assert(result.inputReadyAt != std::chrono::steady_clock::time_point{});
    assert(result.verifiedAt >= result.inputReadyAt);
    assert(backend.openCalls == 2);
    assert(backend.updateCalls == 2);
    assert(backend.closeCalls == 1);
    assert(backend.connected());
    assert(backend.lastInput == initial);
    assert(probeCalls == 2);
    assert(removalWaits == 1);
}

void testVerifiedStartupFailsClosed() {
    using namespace asb::dualsense;

    FakeVirtualDualSense backend;
    std::size_t probeCalls = 0;
    std::size_t removalWaits = 0;
    VirtualDualSenseStartupResult result{};
    std::string error;
    const bool started = startVerifiedVirtualDualSense(
        backend, {}, {},
        [&probeCalls](std::string& probeError)
            -> std::optional<DualSenseFirmwareInfo> {
            ++probeCalls;
            probeError = "synthetic HID publication timeout";
            return std::nullopt;
        },
        [&removalWaits](std::string&) {
            ++removalWaits;
            return true;
        },
        2, result, error);

    assert(!started);
    assert(!backend.connected());
    assert(result.failure == VirtualDualSenseStartupFailure::ReadinessVerification);
    assert(result.attempts == 2);
    assert(!result.firmware);
    assert(error.find("after 2 attempts") != std::string::npos);
    assert(error.find("synthetic HID publication timeout") != std::string::npos);
    assert(backend.openCalls == 2);
    assert(backend.updateCalls == 2);
    assert(backend.closeCalls == 2);
    assert(probeCalls == 2);
    assert(removalWaits == 1);
}

void testVerifiedStartupStopsWhenRemovalFails() {
    using namespace asb::dualsense;

    FakeVirtualDualSense backend;
    VirtualDualSenseStartupResult result{};
    std::string error;
    const bool started = startVerifiedVirtualDualSense(
        backend, {}, {},
        [](std::string& probeError) -> std::optional<DualSenseFirmwareInfo> {
            probeError = "synthetic readiness failure";
            return std::nullopt;
        },
        [](std::string& removalError) {
            removalError = "synthetic removal timeout";
            return false;
        },
        2, result, error);

    assert(!started);
    assert(!backend.connected());
    assert(result.failure == VirtualDualSenseStartupFailure::DeviceRemoval);
    assert(result.attempts == 1);
    assert(error.find("could not be removed") != std::string::npos);
    assert(error.find("synthetic removal timeout") != std::string::npos);
    assert(backend.openCalls == 1);
    assert(backend.updateCalls == 1);
    assert(backend.closeCalls == 1);
}

void testVerifiedStartupDoesNotRetryHardFailures() {
    using namespace asb::dualsense;

    VirtualDualSenseStartupResult result{};
    std::string error;
    FakeVirtualDualSense openFailure;
    openFailure.openFailures = 1;
    assert(!startVerifiedVirtualDualSense(
        openFailure, {}, {},
        [](std::string&) { return std::optional<DualSenseFirmwareInfo>{}; },
        [](std::string&) { return true; }, 2, result, error));
    assert(result.failure == VirtualDualSenseStartupFailure::BackendOpen);
    assert(openFailure.openCalls == 1);
    assert(openFailure.updateCalls == 0);
    assert(openFailure.closeCalls == 1);

    FakeVirtualDualSense inputFailure;
    inputFailure.updateFails = true;
    assert(!startVerifiedVirtualDualSense(
        inputFailure, {}, {},
        [](std::string&) { return std::optional<DualSenseFirmwareInfo>{}; },
        [](std::string&) { return true; }, 2, result, error));
    assert(result.failure == VirtualDualSenseStartupFailure::InitialInput);
    assert(inputFailure.openCalls == 1);
    assert(inputFailure.updateCalls == 1);
    assert(inputFailure.closeCalls == 1);
}


void testUsbInputReportLayout() {
    using namespace asb::dualsense;

    DualSenseUsbReportCounters counters{};
    const auto neutral = buildDualSenseUsbInputReport(DualSenseInputState{}, counters);
    assert(neutral.size() == kUsbInputReportSize);
    assert(neutral[0] == kUsbInputReportId);
    assert(neutral[1] == 0x80 && neutral[2] == 0x80);
    assert(neutral[3] == 0x80 && neutral[4] == 0x80);
    assert(neutral[5] == 0 && neutral[6] == 0);
    assert(neutral[7] == 0);
    // Hat neutral, no face buttons.
    assert(neutral[8] == 0x08);
    assert(neutral[9] == 0 && neutral[10] == 0 && neutral[11] == 0);
    // Both touch points idle.
    assert((neutral[33] & 0x80) != 0);
    assert((neutral[37] & 0x80) != 0);
    // Gravity has to point somewhere: an all-zero accelerometer describes free
    // fall and leaves a host with no orientation reference at all.
    assert(neutral[22] == 0x00 && neutral[23] == 0x00);   // accel X
    assert(neutral[24] == 0x00 && neutral[25] == 0x00);   // accel Y
    assert(neutral[26] == 0x00 && neutral[27] == 0x20);   // accel Z = +8192 (1 g)
    // The gyroscope stays at zero, which is correct: the pad is not turning.
    for (std::size_t offset = 16; offset < 22; ++offset) {
        assert(neutral[offset] == 0x00);
    }
    // Full battery, discharging, wired.
    assert(neutral[53] == 0x0A);
    assert(neutral[54] == 0x08);

    // The hat encodes all eight directions plus neutral.
    const std::array<std::pair<std::uint8_t, std::uint8_t>, 9> hatTable{{
        {0x01, 0x00}, // up
        {0x09, 0x01}, // up + right
        {0x08, 0x02}, // right
        {0x0A, 0x03}, // down + right
        {0x02, 0x04}, // down
        {0x06, 0x05}, // down + left
        {0x04, 0x06}, // left
        {0x05, 0x07}, // up + left
        {0x00, 0x08}, // neutral
    }};
    for (const auto& [dpad, hat] : hatTable) {
        DualSenseInputState state{};
        state.dpad = dpad;
        const auto report = buildDualSenseUsbInputReport(state, counters);
        assert((report[8] & 0x0F) == hat);
    }

    // Face buttons live in the high nibble of byte 8.
    const std::array<std::pair<std::uint16_t, std::uint8_t>, 4> faceTable{{
        {button::kSquare, 0x10},
        {button::kCross, 0x20},
        {button::kCircle, 0x40},
        {button::kTriangle, 0x80},
    }};
    for (const auto& [mask, expected] : faceTable) {
        DualSenseInputState state{};
        state.buttons = mask;
        const auto report = buildDualSenseUsbInputReport(state, counters);
        assert((report[8] & 0xF0) == expected);
    }

    const std::array<std::pair<std::uint16_t, std::uint8_t>, 8> shoulderTable{{
        {button::kL1, 0x01}, {button::kR1, 0x02},
        {button::kL2, 0x04}, {button::kR2, 0x08},
        {button::kCreate, 0x10}, {button::kOptions, 0x20},
        {button::kL3, 0x40}, {button::kR3, 0x80},
    }};
    for (const auto& [mask, expected] : shoulderTable) {
        DualSenseInputState state{};
        state.buttons = mask;
        const auto report = buildDualSenseUsbInputReport(state, counters);
        assert(report[9] == expected);
    }

    const std::array<std::pair<std::uint16_t, std::uint8_t>, 3> systemTable{{
        {button::kPs, 0x01},
        {button::kTouchpadClick, 0x02},
        {button::kMute, 0x04},
    }};
    for (const auto& [mask, expected] : systemTable) {
        DualSenseInputState state{};
        state.buttons = mask;
        const auto report = buildDualSenseUsbInputReport(state, counters);
        assert(report[10] == expected);
    }
}

void testUsbInputReportCountersAndTouch() {
    using namespace asb::dualsense;

    DualSenseUsbReportCounters counters{};
    counters.sequence = 0x17;
    counters.packetSequence = 0x04030201;
    counters.sensorTimestamp = 0x0A0B0C0D;
    counters.touch1Tracking = 0x05;
    counters.touch2Tracking = 0x06;

    DualSenseInputState state{};
    state.touch1Active = true;
    state.touch1X = 0x123;
    state.touch1Y = 0x45;
    state.touch2Active = false;
    state.batteryPercent = 55;
    state.chargeState = 2;

    const auto report = buildDualSenseUsbInputReport(state, counters);
    assert(report[7] == 0x17);
    assert(report[12] == 0x01 && report[13] == 0x02 &&
           report[14] == 0x03 && report[15] == 0x04);
    assert(report[28] == 0x0D && report[29] == 0x0C &&
           report[30] == 0x0B && report[31] == 0x0A);
    assert(report[49] == 0x0D && report[50] == 0x0C &&
           report[51] == 0x0B && report[52] == 0x0A);

    // Active point keeps the tracking id; 12 bits of X then 12 bits of Y.
    assert(report[33] == 0x05);
    assert(report[34] == 0x23);
    assert(report[35] == ((0x01) | (0x05 << 4)));
    assert(report[36] == (0x45 >> 4));
    // Inactive point sets the high bit and keeps its own tracking id.
    assert(report[37] == (0x06 | 0x80));

    // Coordinates are clamped to the physical touchpad.
    DualSenseInputState clamped{};
    clamped.touch1Active = true;
    clamped.touch1X = 5000;
    clamped.touch1Y = 5000;
    const auto clampedReport = buildDualSenseUsbInputReport(clamped, counters);
    const std::uint16_t decodedX =
        static_cast<std::uint16_t>(clampedReport[34] |
                                   ((clampedReport[35] & 0x0F) << 8));
    const std::uint16_t decodedY =
        static_cast<std::uint16_t>((clampedReport[35] >> 4) |
                                   (clampedReport[36] << 4));
    assert(decodedX == 1919);
    assert(decodedY == 1069);

    // Battery: 55 % floors to level 5, charge state 2 means charging.
    assert(report[53] == ((0x01 << 4) | 0x05));

    DualSenseUsbReportCounters ticking{};
    ticking.sequence = 0xFF;
    ticking.packetSequence = 0xFFFFFFFF;
    ticking.sensorTimestamp = 10;
    ticking.advance(3);
    assert(ticking.sequence == 0);
    assert(ticking.packetSequence == 0);
    assert(ticking.sensorTimestamp == 13);
}

void testUsbOutputReportDecoding() {
    using namespace asb::dualsense;

    // Field alignment vector ported verbatim from the VIIPER fork's
    // TestASBShortUSBOutputReportKeepsFieldAlignment: the 48-byte transfer some
    // games submit through SET_REPORT instead of padding to 64 bytes.
    std::array<std::uint8_t, 48> report{};
    report[0] = kUsbOutputReportId;
    report[1] = 0x0F;
    report[2] = 0xD7;
    report[3] = 68;
    report[4] = 215;
    report[11] = 0x21;
    report[21] = 0xAA;
    report[22] = 0x05;
    report[32] = 0xBB;
    report[39] = 0x04;

    DualSenseFeedback feedback{};
    assert(decodeDualSenseOutputReport(report, feedback));
    assert(feedback.kind == FeedbackKind::HidOutput);
    assert(feedback.enableBits1 == 0x0F);
    assert(feedback.enableBits2 == 0xD7);
    assert(feedback.rumbleRight == 68);
    assert(feedback.rumbleLeft == 215);
    assert(feedback.rightTriggerEffect[0] == 0x21);
    assert(feedback.rightTriggerEffect[10] == 0xAA);
    assert(feedback.leftTriggerEffect[0] == 0x05);
    assert(feedback.leftTriggerEffect[10] == 0xBB);
    assert(feedback.enableBits3 == 0x04);
    assert(feedback.hasRumble());
    assert(feedback.requestsRumbleUpdate());
    assert(feedback.hasTriggerEffect());

    // The padded 64-byte USB form decodes to exactly the same fields.
    std::array<std::uint8_t, 64> padded{};
    std::copy(report.begin(), report.end(), padded.begin());
    DualSenseFeedback paddedFeedback{};
    assert(decodeDualSenseOutputReport(padded, paddedFeedback));
    assert(paddedFeedback.enableBits1 == feedback.enableBits1);
    assert(paddedFeedback.rumbleLeft == feedback.rumbleLeft);
    assert(paddedFeedback.rightTriggerEffect == feedback.rightTriggerEffect);
    assert(paddedFeedback.leftTriggerEffect == feedback.leftTriggerEffect);
    assert(paddedFeedback.enableBits3 == feedback.enableBits3);

    // Bluetooth 0x31 carries a two-byte prefix ahead of the same payload.
    std::array<std::uint8_t, 78> bluetooth{};
    bluetooth[0] = kBluetoothOutputReportId;
    std::copy(report.begin() + 1, report.end(), bluetooth.begin() + 3);
    DualSenseFeedback bluetoothFeedback{};
    assert(decodeDualSenseOutputReport(bluetooth, bluetoothFeedback));
    assert(bluetoothFeedback.rumbleRight == 68);
    assert(bluetoothFeedback.leftTriggerEffect[10] == 0xBB);

    // A neutral report is still valid, just without work to do.
    std::array<std::uint8_t, 48> idle{};
    idle[0] = kUsbOutputReportId;
    DualSenseFeedback idleFeedback{};
    assert(decodeDualSenseOutputReport(idle, idleFeedback));
    assert(!idleFeedback.hasRumble());
    assert(!idleFeedback.hasTriggerEffect());
    assert(!idleFeedback.requestsRumbleUpdate());

    // Anything shorter than the common output block is rejected outright.
    const std::array<std::uint8_t, 12> truncated{{kUsbOutputReportId}};
    DualSenseFeedback untouched{};
    untouched.rumbleLeft = 99;
    assert(!decodeDualSenseOutputReport(truncated, untouched));
    assert(untouched.rumbleLeft == 99);
}

void testDescriptorAndFeatureReports() {
    using namespace asb::dualsense;

    const auto descriptor = usbReportDescriptor();
    assert(descriptor.size() == kUsbReportDescriptorSize);
    // Generic Desktop / Game Pad / Application collection, input report 0x01.
    assert(descriptor[0] == 0x05 && descriptor[1] == 0x01);
    assert(descriptor[2] == 0x09 && descriptor[3] == 0x05);
    assert(descriptor[4] == 0xA1 && descriptor[5] == 0x01);
    assert(descriptor[6] == 0x85 && descriptor[7] == 0x01);

    const auto calibration = featureReport(kFeatureReportIdCalibration);
    assert(calibration.size() == kCalibrationFeatureReportSize);
    assert(calibration[0] == kFeatureReportIdCalibration);

    const auto pairing = featureReport(kFeatureReportIdPairingInfo);
    assert(pairing.size() == kPairingInfoFeatureReportSize);
    assert(pairing[0] == kFeatureReportIdPairingInfo);
    // Bytes 7..9 are the fixed marker that follows the controller MAC.
    assert(pairing[7] == 0x08 && pairing[8] == 0x25 && pairing[9] == 0x00);

    const auto firmware = featureReport(kFeatureReportIdFirmware);
    assert(firmware.size() == kFirmwareFeatureReportSize);
    assert(firmware[0] == kFeatureReportIdFirmware);
    // The update-version word the bridge's own firmware check reads.
    const auto updateVersion = static_cast<std::uint16_t>(
        firmware[44] | (firmware[45] << 8));
    assert(updateVersion == 0x0630);
    // The same decoder the CLI uses must accept the served report.
    const auto decoded = decodeFirmwareFeatureReport(firmware);
    assert(decoded.has_value());
    assert(decoded->updateVersion == 0x0630);

    assert(featureReport(0x77).empty());
}

void runNewCodecTests() {
    testUsbInputReportLayout();
    testUsbInputReportCountersAndTouch();
    testUsbOutputReportDecoding();
    testDescriptorAndFeatureReports();
}

} // namespace

int main() {
    runNewCodecTests();

    using namespace asb::dualsense;

    const auto neutral = buildNeutralViiperInput();
    static_assert(neutral.size() == 33);
    assert(neutral[0] == 0x80);
    assert(neutral[1] == 0x80);
    assert(neutral[2] == 0x80);
    assert(neutral[3] == 0x80);
    assert(neutral[4] == 0);
    assert(neutral[5] == 0);
    assert(neutral[31] == 100);
    assert(neutral[32] == 0);

    DualSenseInputState live{};
    live.lx = 1;
    live.ly = 2;
    live.rx = 3;
    live.ry = 4;
    live.l2 = 5;
    live.r2 = 6;
    live.dpad = 0x09;
    live.buttons = 0xA55A;
    live.touch1X = 0x1234;
    live.touch1Y = 0x5678;
    live.touch1Active = true;
    live.touch2X = 0x9ABC;
    live.touch2Y = 0xDEF0;
    live.touch2Active = true;
    live.batteryPercent = 87;
    live.chargeState = 4;
    const auto encodedLive = buildViiperInput(live);
    assert(encodedLive[0] == 1 && encodedLive[5] == 6);
    assert(encodedLive[6] == 0x09);
    assert(encodedLive[7] == 0x5A && encodedLive[8] == 0xA5);
    assert(encodedLive[9] == 0x34 && encodedLive[10] == 0x12);
    assert(encodedLive[11] == 0x78 && encodedLive[12] == 0x56);
    assert(encodedLive[13] == 1);
    assert(encodedLive[14] == 0xBC && encodedLive[15] == 0x9A);
    assert(encodedLive[16] == 0xF0 && encodedLive[17] == 0xDE);
    assert(encodedLive[18] == 1);
    assert(encodedLive[31] == 87 && encodedLive[32] == 4);

    std::array<std::uint8_t, 27> hidPayload{};
    hidPayload[0] = 0x0F;
    hidPayload[2] = 17;
    hidPayload[3] = 42;
    hidPayload[5] = 0x21;
    hidPayload[16] = 0x05;

    DualSenseFeedback feedback{};
    assert(decodeViiperFeedbackFrame(0x01, hidPayload, feedback));
    assert(feedback.kind == FeedbackKind::HidOutput);
    assert(feedback.rumbleRight == 17);
    assert(feedback.rumbleLeft == 42);
    assert(feedback.rightTriggerEffect[0] == 0x21);
    assert(feedback.leftTriggerEffect[0] == 0x05);
    assert(feedback.hasRumble());
    assert(feedback.requestsRumbleUpdate());
    assert(feedback.hasTriggerEffect());

    std::array<std::uint8_t, 26> shortHid{};
    assert(!decodeViiperFeedbackFrame(0x01, shortHid, feedback));
    assert(!decodeViiperFeedbackFrame(0x7F, hidPayload, feedback));

    const std::array<std::uint8_t, 16> audioPayload{
        0x78, 0x56, 0x34, 0x12,
        1, 0, 2, 0,
        3, 0, 4, 0,
        5, 0, 6, 0,
    };
    assert(decodeViiperFeedbackFrame(0x02, audioPayload, feedback));
    assert(feedback.kind == FeedbackKind::AudioHaptics);
    assert(feedback.audioSequence == 0x12345678);
    assert(feedback.leftEnergy == 1);
    assert(feedback.rightEnergy == 2);
    assert(feedback.leftPeak == 3);
    assert(feedback.rightPeak == 4);
    assert(feedback.leftTransient == 5);
    assert(feedback.rightTransient == 6);
    assert(!feedback.hasRumble());
    assert(!feedback.requestsRumbleUpdate());
    assert(!feedback.hasTriggerEffect());

    std::array<std::uint8_t, 64> firmwareReport{};
    firmwareReport[0] = 0x20;
    const std::string buildDate = "Jul  4 2025";
    const std::string buildTime = "10:10:32";
    std::copy(buildDate.begin(), buildDate.end(), firmwareReport.begin() + 1);
    std::copy(buildTime.begin(), buildTime.end(), firmwareReport.begin() + 12);
    firmwareReport[20] = 0x03;
    firmwareReport[22] = 0x04;
    firmwareReport[24] = 0x10;
    firmwareReport[25] = 0x13;
    firmwareReport[28] = 0x2A;
    firmwareReport[30] = 0x10;
    firmwareReport[31] = 0x01;
    firmwareReport[32] = 0x01;
    firmwareReport[33] = 0xC8;
    firmwareReport[44] = 0x30;
    firmwareReport[45] = 0x06;
    const auto firmware = decodeFirmwareFeatureReport(firmwareReport);
    assert(firmware);
    assert(firmware->buildDate == buildDate);
    assert(firmware->buildTime == buildTime);
    assert(firmware->firmwareType == 0x0003);
    assert(firmware->softwareSeries == 0x0004);
    assert(firmware->hardwareInfo == 0x00001310);
    assert(firmware->firmwareVersion == 0x0110002A);
    assert(firmware->updateVersion == 0x0630);
    firmwareReport[0] = 0x21;
    assert(!decodeFirmwareFeatureReport(firmwareReport));

    DualSenseFeedback stopRumble{};
    stopRumble.enableBits1 = 0x01;
    assert(!stopRumble.hasRumble());
    assert(stopRumble.requestsRumbleUpdate());

    DualSenseFeedback residualRumble{};
    residualRumble.rumbleLeft = 99;
    assert(residualRumble.hasRumble());
    assert(!residualRumble.requestsRumbleUpdate());

    DualSenseFeedback hapticsSelectOnly{};
    hapticsSelectOnly.enableBits1 = 0x02;
    hapticsSelectOnly.rumbleLeft = 99;
    hapticsSelectOnly.rumbleRight = 88;
    assert(hapticsSelectOnly.hasRumble());
    assert(!hapticsSelectOnly.requestsRumbleUpdate());

    DualSenseFeedback compatibleVibrationV2{};
    compatibleVibrationV2.enableBits3 = 0x04;
    compatibleVibrationV2.rumbleLeft = 91;
    compatibleVibrationV2.rumbleRight = 37;
    assert(compatibleVibrationV2.hasRumble());
    assert(compatibleVibrationV2.requestsRumbleUpdate());

    const std::string request = viiper::buildRequest("bus/create", "0");
    assert(request.size() == 13);
    assert(request.substr(0, 12) == "bus/create 0");
    assert(request.back() == '\0');
    assert(viiper::buildStreamPath(7, "device-1") == std::string("bus/7/device-1\0", 15));

    std::string server;
    std::string version;
    assert(viiper::parsePingResponse(
        R"({"server":"VIIPER","version":"v0.6.1-steamless8"})", server, version));
    assert(server == "VIIPER");
    assert(version == "v0.6.1-steamless8");
    assert(viiper::isDualSenseCompatibleVersion(version));
    assert(viiper::isDualSenseCompatibleVersion("v0.6.1-steamless5"));
    assert(viiper::isDualSenseCompatibleVersion("v0.6.1-steamless9"));
    assert(viiper::isDualSenseCompatibleVersion("v0.7.0-asb3"));
    assert(!viiper::isDualSenseCompatibleVersion("v0.6.1"));
    assert(!viiper::isDualSenseCompatibleVersion("v0.6.1-steamless4"));
    assert(!viiper::isDualSenseCompatibleVersion("v0.6.2"));
    assert(!viiper::isDualSenseCompatibleVersion("v0.7.0"));
    assert(!viiper::isDualSenseCompatibleVersion("v0.7.0-asb0"));
    assert(!viiper::isDualSenseCompatibleVersion("v0.7.0-notasb1"));
    assert(viiper::isDualSenseCompatibleVersion("v0.7.0-asb1"));
    assert(viiper::isDualSenseCompatibleVersion("v0.8.0-asb2"));

    testVerifiedStartupRetriesReadinessFailure();
    testVerifiedStartupFailsClosed();
    testVerifiedStartupStopsWhenRemovalFails();
    testVerifiedStartupDoesNotRetryHardFailures();

    std::uint32_t busId = 0;
    std::string deviceId;
    assert(viiper::parseBusResponse(R"({"busId":12})", busId));
    assert(busId == 12);
    assert(viiper::parseDeviceResponse(R"({"busId":12,"devId":"ds5-abc"})",
                                       busId, deviceId));
    assert(busId == 12);
    assert(deviceId == "ds5-abc");
    assert(viiper::isUsbIpDriverMissingResponse(
        R"({"detail":"usbip-win2 driver not found"})"));
    assert(!viiper::isUsbIpDriverMissingResponse(
        R"({"detail":"Failed to auto-attach device: ABI mismatch"})"));

    return 0;
}
