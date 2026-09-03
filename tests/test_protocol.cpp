#ifdef NDEBUG
#undef NDEBUG
#endif

#include "flydigi/Apex4Input.h"
#include "flydigi/Apex4Protocol.h"
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

    const auto apex4Normal = buildApex4Normal(TriggerSide::Right);
    assert(apex4Normal.size() == 15);
    assert(apex4Normal[0] == 0x05);
    assert(apex4Normal[1] == 0xA0);
    assert(apex4Normal[2] == 1);
    assert(apex4Normal[3] == 1);
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
    assert(apex4Race[3] == 1);
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
    assert(apex4Raw[3] == 1);
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

    apex4Input[1] = 0;
    assert(!decodeApex4InputReport(apex4Input));

    std::cout << "Protocol tests passed\n";
    return 0;
}
