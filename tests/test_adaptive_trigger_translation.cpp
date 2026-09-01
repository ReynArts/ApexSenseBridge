#include "dualsense/AdaptiveTriggerTranslation.h"

#include <array>
#include <cassert>

int main() {
    using namespace asb;
    std::array<std::uint8_t, 11> effect{};

    effect = {1, 25, 40};
    auto command = dualsense::translateAdaptiveTrigger(TriggerSide::Right, effect, 0);
    assert(command && command->mode == TriggerMode::Race);
    assert((command->params == std::array<std::uint8_t, 5>{25, 40, 0, 0, 0}));

    effect = {37, 20, 0, 3};
    command = dualsense::translateAdaptiveTrigger(TriggerSide::Right, effect, 0);
    assert(command && command->mode == TriggerMode::SniperBreak);
    assert((command->params == std::array<std::uint8_t, 5>{50, 30, 1, 0, 1}));

    effect = {37, 20, 0, 9};
    command = dualsense::translateAdaptiveTrigger(TriggerSide::Right, effect, 0);
    assert(command && command->mode == TriggerMode::RecoilRattle);
    assert(command->params[4] == 10); // raw fifth byte must not collapse to bool

    effect = {33, 128, 3, 0, 0, 77, 9};
    command = dualsense::translateAdaptiveTrigger(TriggerSide::Left, effect, 0);
    assert(command && command->mode == TriggerMode::Race);
    assert(command->params[0] == 128 && command->params[1] == 77);

    effect = {38, 240, 3, 0, 0};
    command = dualsense::translateAdaptiveTrigger(TriggerSide::Left, effect, 61);
    assert(command && command->mode == TriggerMode::Race);
    assert(command->params[0] == 30 && command->params[1] == 61);

    effect = {38, 255, 3, 0, 255};
    assert(!dualsense::translateAdaptiveTrigger(TriggerSide::Left, effect, 0));

    effect = {99};
    assert(!dualsense::translateAdaptiveTrigger(TriggerSide::Right, effect, 0));

    effect = {5};
    command = dualsense::translateAdaptiveTrigger(TriggerSide::Right, effect, 0);
    assert(command && command->mode == TriggerMode::Normal);
}
