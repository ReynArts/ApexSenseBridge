#ifdef NDEBUG
#undef NDEBUG
#endif

#include "flydigi/Apex4Input.h"
#include "flydigi/Apex4Protocol.h"
#include "flydigi/Apex5Input.h"
#include "flydigi/Apex5Protocol.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string_view>

int main() {
    using namespace asb;
    using namespace asb::flydigi;

    assert(isControllerProduct(0x2501));
    assert(isControllerProduct(0x2ABC));
    assert(!isControllerProduct(0x6501));

    assert(isApex4Product(0x04B4, 0x2412));
    assert(!isApex4Product(0x04B4, 0x2411));
    assert(!isApex4Product(0x37D7, 0x2412));

    HidDeviceInfo apex4Descriptor{};
    apex4Descriptor.vendorId = kApex4VendorId;
    apex4Descriptor.productId = kApex4ProductId;
    assert(classifyApex4TriggerInterface(apex4Descriptor) ==
           Apex4TriggerInterfaceCapability::Unknown);
    apex4Descriptor.outputReportLength = 32;
    assert(classifyApex4TriggerInterface(apex4Descriptor) ==
           Apex4TriggerInterfaceCapability::Degraded32Byte);
    apex4Descriptor.outputReportLength = 64;
    assert(classifyApex4TriggerInterface(apex4Descriptor) ==
           Apex4TriggerInterfaceCapability::Full64Byte);
    apex4Descriptor.productId = 0x2411;
    assert(classifyApex4TriggerInterface(apex4Descriptor) ==
           Apex4TriggerInterfaceCapability::NotApplicable);

    dualsense::DualSenseInputState tunedGyro{};
    tunedGyro.gyroX = 100;
    tunedGyro.gyroY = -100;
    tunedGyro.gyroZ = 20000;
    tuneApex4Gyroscope(tunedGyro, 200, 150);
    assert(tunedGyro.gyroX == 200);
    assert(tunedGyro.gyroY == -300);
    assert(tunedGyro.gyroZ == 32767);

    const auto apex4Identity = buildApex4IdentityRequest();
    assert(apex4Identity.size() == 12);
    assert(apex4Identity[0] == 0x05);
    assert(apex4Identity[1] == 0xEC);
    for (std::size_t index = 2; index < apex4Identity.size(); ++index) {
        assert(apex4Identity[index] == 0);
    }

    const auto apex4Normal = buildApex4Normal(TriggerSide::Right);
    assert(apex4Normal.size() == 15);
    assert(apex4Normal[0] == 0x05);
    assert(apex4Normal[1] == 0xA0);
    assert(apex4Normal[2] == 1);
    assert(apex4Normal[3] == 0);
    assert(apex4Normal[4] == 2);
    assert(apex4Normal[5] == 0);

    const auto normal = buildNormal(TriggerSide::Right);
    assert(normal[0] == 0x03);
    assert(normal[1] == 0x5A);
    assert(normal[2] == 0xA5);
    assert(normal[3] == 81);
    assert(normal[4] == 3);
    assert(normal[5] == 1);
    assert(normal[6] == 2);
    assert(normal[7] == 0);

    TriggerEffect race{};
    race.side = TriggerSide::Right;
    race.mode = TriggerMode::Race;
    race.start = 70;
    race.p1 = 30;
    race.matchInput = false;
    const auto raceReport = buildForceTrigger(race);
    assert(raceReport[4] == 6);
    assert(raceReport[5] == 1);
    assert(raceReport[6] == 2);
    assert(raceReport[7] == 1);
    assert(raceReport[8] == 70);
    assert(raceReport[9] == 30);
    assert(raceReport[10] == 0);

    const auto apex4Race = buildApex4ForceTrigger(race);
    assert(apex4Race[0] == 0x05);
    assert(apex4Race[1] == 0xA0);
    assert(apex4Race[2] == 1);
    assert(apex4Race[3] == 0);
    assert(apex4Race[4] == 2);
    assert(apex4Race[5] == 1);
    assert(apex4Race[6] == 70);
    assert(apex4Race[7] == 30);
    assert(apex4Race[8] == 0);

    ForceTriggerCommand apex4RawRace{};
    apex4RawRace.side = TriggerSide::Right;
    apex4RawRace.mode = TriggerMode::Race;
    apex4RawRace.params = {0, 30, 1, 0, 0};
    const auto apex4Raw = buildApex4ForceTriggerRaw(apex4RawRace);
    assert(apex4Raw[2] == 1);
    assert(apex4Raw[3] == 0);
    assert(apex4Raw[4] == 2);
    assert(apex4Raw[5] == 1);
    assert(apex4Raw[6] == 0);
    assert(apex4Raw[7] == 30);
    assert(apex4Raw[8] == 0); // Live Race rewrite clears match-at-zero.

    TriggerEffect rattler{};
    rattler.side = TriggerSide::Left;
    rattler.mode = TriggerMode::RecoilRattle;
    rattler.start = 40;
    rattler.p1 = 0; // builder clamps zero to one
    rattler.p2 = 20;
    rattler.p3 = 35;
    rattler.matchInput = true;
    const auto recoilReport = buildForceTrigger(rattler);
    assert(recoilReport[4] == 8);
    assert(recoilReport[6] == 1);
    assert(recoilReport[7] == 2);
    assert(recoilReport[9] == 1);
    assert(recoilReport[12] == 1);

    const auto rumble = buildRumble(0x34, 0x12);
    assert(rumble[0] == 0x03);
    assert(rumble[1] == 0x5A);
    assert(rumble[2] == 0xA5);
    assert(rumble[3] == 0x12);
    assert(rumble[4] == 6);
    assert(rumble[5] == 0x34);
    assert(rumble[6] == 0x12);
    for (std::size_t index = 7; index < rumble.size(); ++index) {
        assert(rumble[index] == 0);
    }

    const auto profileStatusRequest = buildProfileStatusRequest();
    assert(profileStatusRequest[0] == 0x03);
    assert(profileStatusRequest[1] == 0x5A);
    assert(profileStatusRequest[2] == 0xA5);
    assert(profileStatusRequest[3] == 0xA1);
    assert(profileStatusRequest[4] == 2);
    assert(profileStatusRequest[5] == 0xA3);

    const auto applyProfile = buildApplyProfile(2);
    assert(applyProfile);
    assert((*applyProfile)[0] == 0x03);
    assert((*applyProfile)[3] == 0xA2);
    assert((*applyProfile)[4] == 3);
    assert((*applyProfile)[5] == 2);
    assert((*applyProfile)[6] == 0xA7);
    assert(!buildApplyProfile(4));

    const auto readTransport = buildInputTransportStatusRequest();
    assert(readTransport[3] == kCmdReadInputTransport);
    assert(readTransport[4] == 2);
    assert(readTransport[5] == 0x12);
    const auto enableRaw = buildSetInputTransport(false, true);
    assert(enableRaw[3] == kCmdSetInputTransport);
    assert(enableRaw[4] == 7);
    assert(enableRaw[5] == 0);
    assert(enableRaw[6] == 1);
    assert(enableRaw[7] == 0xFF && enableRaw[8] == 0xFF &&
           enableRaw[9] == 0xFF);
    assert(enableRaw[10] == 0x16);
    // Recovery toggles only raw input, never controller output or keyboard,
    // mouse and third-party ownership. The same builder restores saved flags.
    const auto restartRawOff = buildSetInputTransport(true, false);
    const auto restartRawOn = buildSetInputTransport(true, true);
    assert(restartRawOff[5] == 1 && restartRawOff[6] == 0);
    assert(restartRawOn[5] == 1 && restartRawOn[6] == 1);
    for (std::size_t index = 7; index <= 9; ++index) {
        assert(restartRawOff[index] == 0xFF && restartRawOn[index] == 0xFF);
    }

    std::array<std::uint8_t, 32> transportReply{};
    transportReply[0] = kReportIdIn;
    transportReply[1] = kMagic0;
    transportReply[2] = kMagic1;
    transportReply[3] = kCmdReadInputTransport;
    transportReply[6] = 1;
    transportReply[7] = 0;
    transportReply[8] = 1;
    const auto transport = parseInputTransportStatus(transportReply);
    assert(transport);
    assert(transport->controllerData && !transport->rawData &&
           transport->keyboardData && !transport->mouseData &&
           !transport->thirdPartyControl);
    transportReply[7] = 2;
    assert(!parseInputTransportStatus(transportReply));

    std::array<std::uint8_t, 32> profileReply{};
    profileReply[0] = kReportIdIn;
    profileReply[1] = kMagic0;
    profileReply[2] = kMagic1;
    profileReply[3] = kCmdProfileStatus;
    profileReply[6] = 2;
    assert(isProfileCommandReply(profileReply, kCmdProfileStatus));
    const auto xinputProfile = parseProfileStatus(profileReply);
    assert(xinputProfile);
    assert(xinputProfile->rawSlot == 2);
    assert(xinputProfile->slot == 2);
    assert(!xinputProfile->switchBank);

    profileReply[6] = 6;
    const auto switchProfile = parseProfileStatus(profileReply);
    assert(switchProfile);
    assert(switchProfile->rawSlot == 6);
    assert(switchProfile->slot == 2);
    assert(switchProfile->switchBank);
    profileReply[6] = 8;
    assert(!parseProfileStatus(profileReply));

    std::array<std::uint8_t, 32> apex5Input{};
    apex5Input[0] = kReportIdIn;
    apex5Input[1] = kMagic0;
    apex5Input[2] = kMagic1;
    apex5Input[3] = kCmdOperatorData;
    apex5Input[4] = 0x00; apex5Input[5] = 0x80; // LX -32768.
    apex5Input[6] = 0x00; apex5Input[7] = 0x80; // LY -32768.
    apex5Input[8] = 0xFF; apex5Input[9] = 0x7F; // RX +32767.
    apex5Input[10] = 0xFF; apex5Input[11] = 0x7F; // RY +32767.
    apex5Input[12] = 0x01 | 0x02 | 0x10 | 0x80;
    apex5Input[13] = 0x01 | 0x04;
    apex5Input[15] = 0x08;
    apex5Input[16] = 40;
    apex5Input[17] = 255;
    apex5Input[18] = 0x2E; apex5Input[19] = 0xFB; // Gyro X -1234.
    apex5Input[20] = 0x29; apex5Input[21] = 0x09; // Gyro Y +2345.
    apex5Input[22] = 0x00; apex5Input[23] = 0x80; // Gyro Z -32768.
    apex5Input[24] = 0x00; apex5Input[25] = 0x10; // Accel X +1 g.
    apex5Input[26] = 0x00; apex5Input[27] = 0xF0; // Accel Y -1 g.
    apex5Input[28] = 0x20; apex5Input[29] = 0x4E; // Accel Z clamps high.
    const auto decodedApex5 = decodeApex5InputReport(apex5Input);
    assert(decodedApex5);
    assert(decodedApex5->lx == 0 && decodedApex5->ly == 255);
    assert(decodedApex5->rx == 255 && decodedApex5->ry == 0);
    assert(decodedApex5->l2 == 40 && decodedApex5->r2 == 255);
    assert(decodedApex5->dpad == (0x01 | 0x08));
    assert((decodedApex5->buttons & dualsense::button::kPs) != 0);
    assert((decodedApex5->buttons & dualsense::button::kCross) != 0);
    assert((decodedApex5->buttons & dualsense::button::kSquare) != 0);
    assert((decodedApex5->buttons & dualsense::button::kTriangle) != 0);
    assert((decodedApex5->buttons & dualsense::button::kL1) != 0);
    assert((decodedApex5->buttons & dualsense::button::kL2) != 0);
    assert((decodedApex5->buttons & dualsense::button::kR2) != 0);
    assert(decodedApex5->gyroX == -1234);
    assert(decodedApex5->gyroY == 2345);
    assert(decodedApex5->gyroZ == -32768);
    assert(decodedApex5->accelX == 10000);
    assert(decodedApex5->accelY == -10000);
    assert(decodedApex5->accelZ == 32767);
    assert(decodedApex5->batteryPercent == 100);
    assert(decodedApex5->chargeState == 0);
    const auto decodedApex5Custom = decodeApex5InputReport(apex5Input, 80, 2);
    assert(decodedApex5Custom);
    assert(decodedApex5Custom->batteryPercent == 80);
    assert(decodedApex5Custom->chargeState == 2);

    // The matching game-controller HID collection contains the complete
    // onboard Space Station mapping. It replaces all standard controls while
    // retaining vendor-only PS, motion and battery data.
    auto mergedVendorState = *decodedApex5Custom;
    dualsense::DualSenseInputState mappedHidState{};
    mappedHidState.lx = 200;
    mappedHidState.l2 = 90;
    mappedHidState.r2 = 91;
    mappedHidState.dpad = 0x04;
    mappedHidState.buttons = dualsense::button::kCircle |
                             dualsense::button::kL2 |
                             dualsense::button::kR2;
    mergeApex5MappedControls(mergedVendorState, mappedHidState);
    assert(mergedVendorState.lx == 200);
    assert(mergedVendorState.l2 == 90);
    assert(mergedVendorState.r2 == 91);
    assert(mergedVendorState.dpad == 0x04);
    assert(mergedVendorState.buttons ==
           (dualsense::button::kCircle | dualsense::button::kL2 |
            dualsense::button::kR2 | dualsense::button::kPs));
    assert(mergedVendorState.gyroX == decodedApex5Custom->gyroX);
    assert(mergedVendorState.batteryPercent == 80);

    // A neutral mapped state is meaningful (for example a physical button
    // mapped only to a keyboard key) and must not leak the raw vendor button
    // into the virtual DualSense.
    auto neutralMappedState = *decodedApex5Custom;
    mergeApex5MappedControls(
        neutralMappedState, dualsense::DualSenseInputState{});
    assert(neutralMappedState.lx == 0x80);
    assert(neutralMappedState.ly == 0x80);
    assert(neutralMappedState.rx == 0x80);
    assert(neutralMappedState.ry == 0x80);
    assert(neutralMappedState.l2 == 0);
    assert(neutralMappedState.r2 == 0);
    assert(neutralMappedState.dpad == 0);
    assert(neutralMappedState.buttons == dualsense::button::kPs);
    assert(neutralMappedState.gyroX == decodedApex5Custom->gyroX);
    assert(neutralMappedState.batteryPercent == 80);

    const auto mappedOnlyState = composeApex5InputState(
        mappedHidState, std::nullopt, 67, 1);
    assert(mappedOnlyState.lx == mappedHidState.lx);
    assert(mappedOnlyState.l2 == mappedHidState.l2);
    assert(mappedOnlyState.r2 == mappedHidState.r2);
    assert(mappedOnlyState.buttons == mappedHidState.buttons);
    assert(mappedOnlyState.gyroX == 0);
    assert(mappedOnlyState.batteryPercent == 67);
    assert(mappedOnlyState.chargeState == 1);

    const auto composedState = composeApex5InputState(
        mappedHidState, decodedApex5Custom, 74, 2);
    assert(composedState.lx == mappedHidState.lx);
    assert(composedState.l2 == decodedApex5Custom->l2);
    assert(composedState.r2 == decodedApex5Custom->r2);
    assert(composedState.buttons ==
           (mappedHidState.buttons | dualsense::button::kPs));
    assert(composedState.gyroX == decodedApex5Custom->gyroX);
    assert(composedState.batteryPercent == 74);
    assert(composedState.chargeState == 2);

    // A single raw vendor trigger must not bypass the mapped Space Station
    // behavior. Only a confirmed simultaneous press uses the independent pair.
    auto vendorLeftOnly = *decodedApex5Custom;
    vendorLeftOnly.l2 = 200;
    vendorLeftOnly.r2 = 0;
    const auto composedLeftOnly = composeApex5InputState(
        mappedHidState, vendorLeftOnly, 74, 2);
    assert(composedLeftOnly.l2 == mappedHidState.l2);
    assert(composedLeftOnly.r2 == mappedHidState.r2);
    assert((composedLeftOnly.buttons & dualsense::button::kL2) != 0);
    assert((composedLeftOnly.buttons & dualsense::button::kR2) != 0);
    assert((composedLeftOnly.buttons & dualsense::button::kCircle) != 0);

    // Reproduce LT held -> RT pressed on a centered HID axis. Mapped controls
    // are neutral when the triggers cancel, but a live operator pair recovers
    // both; releasing RT restores the mapped LT and preserves other mappings.
    dualsense::DualSenseInputState aimMapped{};
    aimMapped.l2 = 220;
    aimMapped.buttons = dualsense::button::kL2 | dualsense::button::kCircle;
    auto aimVendor = vendorLeftOnly;
    aimVendor.l2 = 220;
    const auto aiming = composeApex5InputState(aimMapped, aimVendor, 74, 2);
    assert(aiming.l2 == 220 && aiming.r2 == 0);
    auto canceledMapped = aimMapped;
    canceledMapped.l2 = 0;
    canceledMapped.buttons = dualsense::button::kCircle;
    auto firingVendor = aimVendor;
    firingVendor.r2 = 210;
    const auto aimingAndFiring = composeApex5InputState(canceledMapped, firingVendor, 74, 2);
    assert(aimingAndFiring.l2 == 220 && aimingAndFiring.r2 == 210);
    assert((aimingAndFiring.buttons & dualsense::button::kL2) != 0);
    assert((aimingAndFiring.buttons & dualsense::button::kR2) != 0);
    assert((aimingAndFiring.buttons & dualsense::button::kCircle) != 0);
    const auto firingReleased = composeApex5InputState(aimMapped, aimVendor, 74, 2);
    assert(firingReleased.l2 == 220 && firingReleased.r2 == 0);
    assert((firingReleased.buttons & dualsense::button::kR2) == 0);
    const auto stalePairDropped = composeApex5InputState(canceledMapped, std::nullopt, 74, 2);
    assert(stalePairDropped.l2 == 0 && stalePairDropped.r2 == 0);
    assert(stalePairDropped.buttons == dualsense::button::kCircle);

    assert(!decodeApex5InputReport(
        std::span<const std::uint8_t>(apex5Input.data(), 29)));
    apex5Input[3] = 0;
    assert(!decodeApex5InputReport(apex5Input));
    profileReply[0] = 0xEF;
    assert(!isProfileCommandReply(profileReply, kCmdProfileStatus));
    assert(!parseProfileStatus(profileReply));

    const auto apex4Rumble = buildApex4Rumble(0x34, 0x12);
    assert((apex4Rumble == Apex4RumbleReport{0x05, 0x0F, 0x34, 0x12}));

    std::array<std::uint8_t, 32> apex4Input{};
    apex4Input[0] = 0x04;
    apex4Input[1] = 0xFE;
    apex4Input[8] = 0x08;  // Guide.
    apex4Input[9] = 0x01 | 0x02 | 0x10 | 0x80; // Up/right, A, X.
    apex4Input[10] = 0x01 | 0x04; // Y, LB.
    apex4Input[17] = 0x7F;
    apex4Input[19] = 0x20;
    apex4Input[21] = 0xE0;
    apex4Input[22] = 0x7F;
    apex4Input[23] = 40;
    apex4Input[24] = 0;
    // Motion is emitted only when the active Apex 4 profile has gyro mapping
    // enabled. Mouse fields must not override the native 16-bit IMU channels.
    apex4Input[4] = 0x9C;
    apex4Input[5] = 0xF4;
    apex4Input[6] = 0xF1;
    apex4Input[11] = 0x64; // accel X = +100
    apex4Input[12] = 0x00;
    apex4Input[13] = 0x38; // accel Y = -200
    apex4Input[14] = 0xFF;
    apex4Input[15] = 0x20; // accel Z = +800 (approximately 1 g)
    apex4Input[16] = 0x03;
    apex4Input[18] = 0x9C; // yaw=-100, high byte at 20 (19 is a stick).
    apex4Input[20] = 0xFF;
    apex4Input[26] = 0xF4; // pitch=+500
    apex4Input[27] = 0x01;
    apex4Input[29] = 0xD4; // roll = -300
    apex4Input[30] = 0xFE;
    const auto decoded = decodeApex4InputReport(apex4Input);
    assert(decoded);
    assert(decoded->lx == 0x80);
    assert(decoded->ly == 0x20);
    assert(decoded->rx == 0xE0);
    assert(decoded->ry == 0x80);
    assert(decoded->l2 == 40 && decoded->r2 == 0);
    assert(decoded->dpad == (0x01 | 0x08));
    assert((decoded->buttons & dualsense::button::kPs) != 0);
    assert((decoded->buttons & dualsense::button::kCross) != 0);
    assert((decoded->buttons & dualsense::button::kSquare) != 0);
    assert((decoded->buttons & dualsense::button::kTriangle) != 0);
    assert((decoded->buttons & dualsense::button::kL1) != 0);
    assert((decoded->buttons & dualsense::button::kL2) != 0);
    assert(decoded->gyroX == 4500);
    assert(decoded->gyroY == -500);
    assert(decoded->gyroZ == 720);
    assert(decoded->accelX == -1250);
    assert(decoded->accelY == 10000);
    assert(decoded->accelZ == -2500);
    assert(decoded->batteryPercent == 100);
    assert(decoded->chargeState == 0);
    const auto decodedApex4Custom = decodeApex4InputReport(apex4Input, 40, 4);
    assert(decodedApex4Custom);
    assert(decodedApex4Custom->batteryPercent == 40);
    assert(decodedApex4Custom->chargeState == 4);

    // Replay actual reports from the 2026-09-30 captures, including both
    // dongle byte-3 variants. Expected outputs lock the experimental gains.
    struct MotionFixture {
        std::string_view hex;
        std::array<std::int16_t, 6> expected;
    };
    constexpr std::array<MotionFixture, 6> motionFixtures{{
        {"04FE6600000000000000000000000021037F007F007F7F000000000054000000",
         {0, 0, 0, 0, 10012, 0}},
        {"04FE66000050FB0000000025004A002F037E017F007F7F000000960054070000",
         {1350, 5, -16, -462, 10187, 925}},
        {"04FE6600505FFF000000000000DDFF35037F617F017F7F00000016005478FF00",
         {198, 1765, 326, 0, 10262, -437}},
        {"04FE660000000000000000000002001E037F007F007F7F000000000000000000",
         {0, 0, 0, 0, 9975, 25}},
        {"04FE66800140060000000002009DFEEE027FFE7FFF7F7F00000037FF00F6FF00",
         {-1809, -10, 24, -25, 9375, -4437}},
        {"04FE66807FAF01000000005900DCFF2B037F027F017F7F000000CCFF0099FF00",
         {-468, 1290, 247, -1112, 10137, -450}},
    }};
    const auto hexDigit = [](char digit) {
        return digit <= '9' ? digit - '0' : digit - 'A' + 10;
    };
    for (const auto& fixture : motionFixtures) {
        assert(fixture.hex.size() == 64);
        std::array<std::uint8_t, 32> report{};
        for (std::size_t index = 0; index < report.size(); ++index) {
            report[index] = static_cast<std::uint8_t>(
                (hexDigit(fixture.hex[index * 2]) << 4) |
                hexDigit(fixture.hex[index * 2 + 1]));
        }
        const auto result = decodeApex4InputReport(report);
        assert(result);
        const std::array<std::int16_t, 6> actual{
            result->gyroX, result->gyroY, result->gyroZ,
            result->accelX, result->accelY, result->accelZ};
        assert(actual == fixture.expected);
    }

    auto changedControls = apex4Input;
    changedControls[17] = 0;
    changedControls[19] = 255;
    changedControls[4] = changedControls[5] = changedControls[6] = 0;
    const auto unchangedMotion = decodeApex4InputReport(changedControls);
    assert(unchangedMotion);
    assert(unchangedMotion->gyroX == decoded->gyroX);
    assert(unchangedMotion->gyroY == decoded->gyroY);
    assert(unchangedMotion->gyroZ == decoded->gyroZ);

    std::array<std::uint8_t, 32> extremeMotion{};
    extremeMotion[0] = 4;
    extremeMotion[1] = 0xFE;
    extremeMotion[20] = extremeMotion[27] = extremeMotion[30] = 0x80;
    extremeMotion[12] = extremeMotion[14] = extremeMotion[16] = 0x80;
    const auto saturatedMotion = decodeApex4InputReport(extremeMotion);
    assert(saturatedMotion);
    assert(saturatedMotion->gyroX == -32768);
    assert(saturatedMotion->gyroY == -32768);
    assert(saturatedMotion->gyroZ == 32767);
    assert(saturatedMotion->accelX == 32767);
    assert(saturatedMotion->accelY == -32768);
    assert(saturatedMotion->accelZ == -32768);

    extremeMotion.fill(0);
    extremeMotion[0] = 4;
    extremeMotion[1] = 0xFE;
    const auto disabledMotion = decodeApex4InputReport(extremeMotion);
    assert(disabledMotion);
    assert(disabledMotion->gyroX == 0 && disabledMotion->gyroY == 0 &&
           disabledMotion->gyroZ == 0);
    assert(disabledMotion->accelX == 0 && disabledMotion->accelY == 0 &&
           disabledMotion->accelZ == 0);
    assert(!decodeApex4InputReport(
        std::span<const std::uint8_t>(extremeMotion.data(), 31)));
    extremeMotion[1] = 0xFF;
    assert(!decodeApex4InputReport(extremeMotion));

    apex4Input[1] = 0;
    assert(!decodeApex4InputReport(apex4Input));

    const auto readRgbReport = buildReadRgbConfig(0, 20);
    assert(readRgbReport[0] == kReportIdOut);
    assert(readRgbReport[1] == kMagic0);
    assert(readRgbReport[2] == kMagic1);
    assert(readRgbReport[3] == kCmdReadRgbConfig);
    assert(readRgbReport[4] == 4);
    assert(readRgbReport[5] == 0);
    assert(readRgbReport[6] == 20);
    assert(readRgbReport[7] == static_cast<std::uint8_t>(kCmdReadRgbConfig + 4 + 0 + 20));

    const auto writeRgbStartReport = buildWriteRgbStart(0, 0, 19, 20);
    assert(writeRgbStartReport[0] == kReportIdOut);
    assert(writeRgbStartReport[1] == kMagic0);
    assert(writeRgbStartReport[2] == kMagic1);
    assert(writeRgbStartReport[3] == kCmdWriteRgbStart);
    assert(writeRgbStartReport[4] == 6);
    assert(writeRgbStartReport[5] == 0);
    assert(writeRgbStartReport[6] == 0);
    assert(writeRgbStartReport[7] == 19);
    assert(writeRgbStartReport[8] == 20);
    assert(writeRgbStartReport[9] == static_cast<std::uint8_t>(kCmdWriteRgbStart + 6 + 0 + 0 + 19 + 20));

    const std::array<std::uint8_t, 20> samplePackData{1, 2, 3};
    const auto writeRgbPackReport = buildWriteRgbPack(0, samplePackData);
    assert(writeRgbPackReport[0] == kReportIdOut);
    assert(writeRgbPackReport[1] == kMagic0);
    assert(writeRgbPackReport[2] == kMagic1);
    assert(writeRgbPackReport[3] == kCmdWriteRgbPack);
    assert(writeRgbPackReport[4] == 23);
    assert(writeRgbPackReport[5] == 0);
    assert(writeRgbPackReport[6] == 1);
    assert(writeRgbPackReport[7] == 2);
    assert(writeRgbPackReport[8] == 3);

    const auto staticPayload = buildStaticRgbPayload(255, 128, 64, 100);
    assert(staticPayload[0] == 0x00);
    assert(staticPayload[1] == 0x03);
    assert(staticPayload[6] == 100);
    assert(staticPayload[7] == 12);
    assert(staticPayload[8] == 0x04);
    assert(staticPayload[20] == 255);
    assert(staticPayload[21] == 128);
    assert(staticPayload[22] == 64);

    std::cout << "Protocol tests passed\n";
    return 0;
}
