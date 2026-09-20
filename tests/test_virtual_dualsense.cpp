#ifdef NDEBUG
#undef NDEBUG
#endif

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

} // namespace

int main() {
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

    DualSenseFeedback feedbackNoLed{};
    assert(!feedbackNoLed.hasLightbarColor());

    DualSenseFeedback feedbackLed{};
    feedbackLed.kind = FeedbackKind::HidOutput;
    feedbackLed.hasLightbar = true;
    feedbackLed.lightbarRed = 0xFF;
    feedbackLed.lightbarGreen = 0x80;
    feedbackLed.lightbarBlue = 0x00;
    assert(feedbackLed.hasLightbarColor());
    assert(feedbackLed.lightbarRed == 255);
    assert(feedbackLed.lightbarGreen == 128);
    assert(feedbackLed.lightbarBlue == 0);

    assert(toBatteryPercent(0) == 10);
    assert(toBatteryPercent(1) == 20);
    assert(toBatteryPercent(2) == 40);
    assert(toBatteryPercent(3) == 60);
    assert(toBatteryPercent(4) == 80);
    assert(toBatteryPercent(5) == 100);
    assert(toBatteryPercent(6) == 100);

    assert(toChargeState(false, false, 80) == chargeStatus::kDischarging);
    assert(toChargeState(true, false, 80) == chargeStatus::kCharging);
    assert(toChargeState(true, false, 100) == chargeStatus::kFull);
    assert(toChargeState(false, true, 80) == chargeStatus::kCharging);
    assert(toChargeState(false, true, 100) == chargeStatus::kFull);

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
