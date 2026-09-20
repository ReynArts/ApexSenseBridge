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

int main() {
    using namespace asb;
    using namespace asb::flydigi;

    assert(isControllerProduct(0x2501));
    assert(isControllerProduct(0x2ABC));
    assert(!isControllerProduct(0x6501));

    assert(isApex4Product(0x04B4, 0x2412));
    assert(!isApex4Product(0x04B4, 0x2411));
    assert(!isApex4Product(0x37D7, 0x2412));

    const auto apex4Identity = buildApex4IdentityRequest();
    assert(apex4Identity.size() == 12);
    assert(apex4Identity[0] == 0x05);
    assert(apex4Identity[1] == 0xEC);
    for (std::size_t index = 2; index < apex4Identity.size(); ++index) {
        assert(apex4Identity[index] == 0);
    }

    // Byte 3 is Flydigi's apply flag and the bridge deliberately clears it on
    // an Apex 4: a report with it cleared engages and releases the same
    // resistance, while a report with it set stops the pad's whole main loop
    // for about 1073 ms over the 2.4 GHz dongle. Both halves were confirmed
    // blind; see docs/APEX4-DONGLE-TRIGGER-STALL.md. These assertions
    // exist so the flag cannot drift back without someone reading that.
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
    assert(apex4Race[3] == 0);   // apply flag cleared, as above
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
    assert(apex4Raw[3] == 0);    // apply flag cleared, as above
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
    // The checksum sums report[3] through report[3 + length - 1]:
    //   0x11 command + 0x07 length + 0x00 + 0x01 + 0xFF + 0xFF + 0xFF = 0x16.
    // The read request a few lines above validates the same algorithm
    // independently, where 0x10 + 0x02 gives the 0x12 asserted there.
    //
    // This assertion used to read 0x0B, which matches no variant of that sum.
    // It is not a captured value despite looking like one: git shows the
    // assertion, buildSetInputTransport and buildChecksummedCommand all arriving
    // in the same commit, unchanged since, so the expectation never once agreed
    // with the code it shipped beside - the test has been red since it was
    // written. Had 0x0B come from watching Flydigi's own software, the
    // disagreement would have surfaced on its first run.
    //
    // Still worth confirming against an Apex 5, which this project has no access
    // to; nothing here can tell whether the pad accepts what we send.
    assert(enableRaw[10] == 0x16);

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

    // The M1 rear paddle carries Create, which XInput cannot express and which
    // nothing set before. View/Back is unavailable for it: that button carries
    // the touchpad click, since XInput has no touchpad either. Paddle bits were
    // read off the hardware - M1 0x04, M2 0x08, M3 0x10, M4 0x20 in report[7].
    assert((decoded->buttons & dualsense::button::kCreate) == 0);
    apex4Input[7] = 0x04;
    const auto withCreate = decodeApex4InputReport(apex4Input);
    assert(withCreate);
    assert((withCreate->buttons & dualsense::button::kCreate) != 0);
    // Back keeps the touchpad click rather than being traded away for Create.
    assert((withCreate->buttons & dualsense::button::kTouchpadClick) ==
           (decoded->buttons & dualsense::button::kTouchpadClick));
    // The other three paddles stay unmapped on purpose.
    apex4Input[7] = 0x08 | 0x10 | 0x20;
    const auto otherPaddles = decodeApex4InputReport(apex4Input);
    assert(otherPaddles);
    assert((otherPaddles->buttons & dualsense::button::kCreate) == 0);
    apex4Input[7] = 0;

    apex4Input[1] = 0;
    assert(!decodeApex4InputReport(apex4Input));

    std::cout << "Protocol tests passed\n";
    return 0;
}
