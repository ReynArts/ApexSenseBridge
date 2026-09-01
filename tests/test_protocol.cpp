#ifdef NDEBUG
#undef NDEBUG
#endif

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

    std::cout << "Protocol tests passed\n";
    return 0;
}
