#ifdef NDEBUG
#undef NDEBUG
#endif

#include "flydigi/Apex6Protocol.h"

#include <cassert>
#include <cstdint>
#include <span>

int main() {
    using namespace asb::flydigi::apex6;

    assert(isProduct(0x37D7, 0x2502));
    assert(!isProduct(0x37D7, 0x2501));
    assert(!isProduct(0x04B4, 0x2412));

    const auto infoRequest = buildGetInfo();
    assert(infoRequest.size() == 33);
    assert(infoRequest[0] == 0x00);
    assert(infoRequest[1] == 0x5A);
    assert(infoRequest[2] == 0xA5);
    assert(infoRequest[3] == 0x01);
    assert(infoRequest[4] == 0x02);
    assert(infoRequest[32] == 0x03);

    const auto enable = buildMotorEnable();
    assert(enable[0][3] == kCmdMotorRoute);
    assert(enable[0][4] == 0x07);
    assert(enable[0][32] == 0xA0);
    assert(enable[1][32] == 0xAF);

    const auto disable = buildMotorDisable();
    assert(disable[0][4] == 0x05);
    assert(disable[0][32] == 0x5B);
    assert(disable[1][32] == 0x6B);

    MotorBlock motors{};
    motors[0] = MotorSubframe{-128, 0, 127};
    motors[7] = MotorSubframe{1, -1, 0};
    const auto realtime = buildRealtimeMotor(
        motors, TriggerRoute::Right, true, true, true);
    assert(realtime[3] == kCmdRealtimeMotor);
    assert(realtime[4] == 0x1B);
    assert(realtime[5] == 0x9D);
    assert(realtime[6] == 0x00);
    assert(realtime[7] == 0x80);
    assert(realtime[8] == 0xFF);
    assert(realtime[27] == 0x81);
    assert(realtime[28] == 0x7F);
    assert(realtime[29] == 0x80);
    assert(realtime[32] == checksum(
        std::span<const std::uint8_t>(realtime).subspan(1)));

    const auto gripsOnly = buildRealtimeMotor(
        motors, TriggerRoute::Mute, false, true, true);
    assert(gripsOnly[5] == 0x9B);
    assert(gripsOnly[6] == 0x80);

    Report infoReply{};
    infoReply[0] = 0x00;
    infoReply[1] = kMagic0;
    infoReply[2] = kMagic1;
    infoReply[3] = kCmdGetInfo;
    infoReply[4] = 1; // package count
    infoReply[5] = 0; // package index
    infoReply[6] = kDeviceType;
    infoReply[7] = 0; // connection mode
    infoReply[30] = 0x9F;
    infoReply[32] = checksum(
        std::span<const std::uint8_t>(infoReply).subspan(1));
    const auto parsed = parseDeviceInfo(infoReply);
    assert(parsed);
    assert(parsed->deviceType == kDeviceType);
    assert(parsed->features == 0x9F);
    assert(parsed->hasGripHaptics());
    assert(parsed->hasTriggerHaptics());

    auto corrupt = infoReply;
    corrupt[30] ^= 0x01;
    assert(!parseDeviceInfo(corrupt));

    // Some HID stacks strip the leading report-ID byte on input.
    const auto stripped = std::span<const std::uint8_t>(infoReply).subspan(1);
    assert(parseDeviceInfo(stripped));

    assert(!parsed->hasBatteryReading());
    assert(parsed->firmware == FirmwareVersions{});

    const auto sealed = [](Report report) {
        report[32] = checksum(std::span<const std::uint8_t>(report).subspan(1));
        return report;
    };
    auto detailed = infoReply;
    detailed[12] = 0x17;
    for (std::size_t index = 0; index < 14; ++index) {
        detailed[16 + index] = static_cast<std::uint8_t>(index % 2 == 0 ? 0x10 + index / 2 : 0x20 + index / 2);
    }
    const auto detailedInfo = parseDeviceInfo(sealed(detailed));
    assert(detailedInfo);
    assert(detailedInfo->batteryLevel == 7);
    assert(detailedInfo->chargingStatus == kChargingStatusCharging);
    assert(detailedInfo->hasBatteryReading());
    assert(detailedInfo->features == 0x9F);
    assert((detailedInfo->firmware == FirmwareVersions{0x1020, 0x1121, 0x1222, 0x1323, 0x1424, 0x1525, 0x1626}));

    auto full = infoReply;
    full[12] = 0x2A;
    const auto fullInfo = parseDeviceInfo(sealed(full));
    assert(fullInfo && fullInfo->batteryLevel == 10 && fullInfo->chargingStatus == kChargingStatusFull);
    assert(fullInfo->hasBatteryReading());

    auto empty = infoReply;
    empty[12] = 0x10;
    const auto emptyCharging = parseDeviceInfo(sealed(empty));
    assert(emptyCharging && emptyCharging->batteryLevel == 0 && emptyCharging->hasBatteryReading());

    auto outOfRange = infoReply;
    outOfRange[12] = 0x0B;
    const auto outOfRangeInfo = parseDeviceInfo(sealed(outOfRange));
    assert(outOfRangeInfo && outOfRangeInfo->batteryLevel == 11 && !outOfRangeInfo->hasBatteryReading());

    auto unreported = infoReply;
    unreported[12] = 0x00;
    assert(!parseDeviceInfo(sealed(unreported))->hasBatteryReading());

    const auto statusRequest = buildProfileStatusRequest();
    assert(statusRequest.size() == 33 && statusRequest[0] == kReportId);
    assert(statusRequest[3] == kCmdProfileStatus && statusRequest[4] == 0x02);
    assert(statusRequest[32] == 0xA3);

    const auto applySlot0 = buildApplyProfile(0);
    assert(applySlot0 && (*applySlot0)[3] == kCmdApplyProfile);
    assert((*applySlot0)[4] == 0x03 && (*applySlot0)[5] == 0x00 && (*applySlot0)[32] == 0xA5);
    const auto applySlot3 = buildApplyProfile(3);
    assert(applySlot3 && (*applySlot3)[5] == 0x03 && (*applySlot3)[32] == 0xA8);
    assert(!buildApplyProfile(kProfileSlotCount));

    const Report hardwareStatus{
        0x00, 0x5A, 0xA5, 0xA1, 0x01, 0x00, 0x00, 0x12, 0x41, 0x12, 0x41, 0x12, 0x41,
        0x12, 0x41, 0x5D, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5C};
    const Report hardwareApplySlot1{
        0x00, 0x5A, 0xA5, 0xA2, 0x01, 0x00, 0x01, 0x12, 0x41, 0x12, 0x41, 0x12, 0x41,
        0x12, 0x41, 0x5D, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5E};
    const auto state = parseProfileState(hardwareStatus, kCmdProfileStatus);
    assert(state && state->rawSlot == 0 && state->globalChecksum == 0x115D);
    assert((state->slotChecksums == std::array<std::uint16_t, 4>{0x4112, 0x4112, 0x4112, 0x4112}));
    const auto applied = parseProfileState(hardwareApplySlot1, kCmdApplyProfile);
    assert(applied && applied->rawSlot == 1 && applied->slotChecksums == state->slotChecksums);
    assert(!parseProfileState(hardwareStatus, kCmdApplyProfile));
    assert(!parseProfileState(hardwareApplySlot1, kCmdProfileStatus));
    assert(!parseProfileState(hardwareStatus, kCmdGetInfo));
    assert(parseProfileState(std::span<const std::uint8_t>(hardwareStatus).subspan(1), kCmdProfileStatus));

    const auto reseal = [](Report report) {
        report[32] = checksum(std::span<const std::uint8_t>(report).subspan(1));
        return report;
    };
    auto badChecksum = hardwareStatus;
    badChecksum[32] ^= 0x01;
    assert(!parseProfileState(badChecksum, kCmdProfileStatus));
    auto errorReply = hardwareStatus;
    errorReply[4] = 0;
    assert(!parseProfileState(reseal(errorReply), kCmdProfileStatus));
    auto distinct = hardwareStatus;
    distinct[8] = 0x01;
    distinct[10] = 0x02;
    distinct[12] = 0x03;
    distinct[14] = 0x04;
    distinct[16] = 0x05;
    const auto distinctState = parseProfileState(reseal(distinct), kCmdProfileStatus);
    assert(distinctState);
    assert((distinctState->slotChecksums == std::array<std::uint16_t, 4>{0x0112, 0x0212, 0x0312, 0x0412}));
    assert(distinctState->globalChecksum == 0x055D);
    auto switchBank = hardwareStatus;
    switchBank[6] = 5;
    const auto switchBankState = parseProfileState(reseal(switchBank), kCmdProfileStatus);
    assert(switchBankState && switchBankState->rawSlot == 5);
    switchBank[6] = 8;
    assert(!parseProfileState(reseal(switchBank), kCmdProfileStatus));
    assert(!parseProfileState(std::span<const std::uint8_t>(hardwareStatus).first(20), kCmdProfileStatus));
    return 0;
}
