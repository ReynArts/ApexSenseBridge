#ifdef NDEBUG
#undef NDEBUG
#endif

#include "dualsense/DualSenseFeedback.h"
#include "dualsense/DualSenseFirmware.h"
#include "dualsense/DualSenseInput.h"
#include "dualsense/ViiperProtocol.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <string>

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

    return 0;
}
