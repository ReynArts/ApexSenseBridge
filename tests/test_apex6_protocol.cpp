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
    return 0;
}
