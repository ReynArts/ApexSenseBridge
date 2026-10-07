#ifdef NDEBUG
#undef NDEBUG
#endif

#include "dualsense/AdaptiveTriggerTranslation.h"

#include <array>
#include <cassert>

namespace {

void expectCommand(const std::array<std::uint8_t, 11>& effect,
                   asb::TriggerMode mode,
                   const std::array<std::uint8_t, 5>& params) {
    for (const auto side : {asb::TriggerSide::Left, asb::TriggerSide::Right}) {
        for (const auto motor : {std::uint8_t{0}, std::uint8_t{61}, std::uint8_t{255}}) {
            const auto command = asb::dualsense::translateAdaptiveTrigger(side, effect, motor);
            assert(command && command->side == side);
            assert(command->mode == mode);
            assert(command->params == params);
        }
    }
}

void expectUnsupported(const std::array<std::uint8_t, 11>& effect) {
    for (const auto side : {asb::TriggerSide::Left, asb::TriggerSide::Right}) {
        assert(!asb::dualsense::translateAdaptiveTrigger(side, effect, 0));
    }
}

}

int main() {
    using asb::TriggerMode;

    expectCommand({1, 25, 40}, TriggerMode::Race, {25, 40, 0, 0, 0});
    expectCommand({2, 25, 90, 40}, TriggerMode::SniperBreak, {25, 90, 40, 0, 0});
    expectCommand({6, 35, 40, 25}, TriggerMode::RecoilRattle, {25, 40, 40, 35, 0});
    expectCommand({5}, TriggerMode::Normal, {});

    expectCommand({0x21, 0xFC, 0x03}, TriggerMode::Race, {38, 8, 0, 0, 0});
    expectCommand({0x21, 0x80, 0, 0, 0, 0xE0}, TriggerMode::Race, {134, 64, 0, 0, 0});
    expectCommand({0x21, 0, 0x02, 0, 0, 0, 0x38}, TriggerMode::Race, {173, 64, 0, 0, 0});
    expectCommand({0x21, 0x24, 0, 0x40, 0x80, 0x03}, TriggerMode::Race, {38, 64, 0, 0, 0});
    expectCommand({0x21, 0x01, 0, 0, 0, 0, 0x38}, TriggerMode::Race, {0, 8, 0, 0, 0});
    expectCommand({0x21}, TriggerMode::Normal, {});

    expectCommand({0x25, 0x84, 0, 3}, TriggerMode::SniperBreak, {38, 96, 32, 0, 0});
    expectCommand({0x25, 0x14, 0, 3}, TriggerMode::SniperBreak, {38, 39, 32, 0, 0});
    expectCommand({0x25, 0x84}, TriggerMode::SniperBreak, {38, 96, 8, 0, 0});
    expectCommand({0x25, 0x84, 0, 7}, TriggerMode::SniperBreak, {38, 96, 64, 0, 0});
    expectCommand({0x25, 0x01, 0x02, 3}, TriggerMode::SniperBreak, {0, 173, 32, 0, 0});
    expectCommand({0x25, 0, 0x03, 3}, TriggerMode::SniperBreak, {154, 19, 32, 0, 0});
    for (unsigned startZone = 0; startZone < 9; ++startZone) {
        for (unsigned endZone = startZone + 1; endZone < 10; ++endZone) {
            const auto mask = (1U << startZone) | (1U << endZone);
            const auto startStroke = (startZone * 192U + 5U) / 10U;
            const auto endStroke = (endZone * 192U + 5U) / 10U;
            expectCommand({0x25, static_cast<std::uint8_t>(mask & 0xFFU),
                           static_cast<std::uint8_t>(mask >> 8U), 3},
                          TriggerMode::SniperBreak,
                          {static_cast<std::uint8_t>(startStroke),
                           static_cast<std::uint8_t>(endStroke - startStroke), 32, 0, 0});
        }
    }
    expectCommand({0x25}, TriggerMode::Normal, {});
    expectUnsupported({0x25, 0x04});
    expectUnsupported({0x25, 0x94});

    expectCommand({0x26, 0xF0, 0x03, 0, 0, 0, 0, 0, 0, 35},
                  TriggerMode::RecoilRattle, {77, 1, 15, 35, 0});
    expectCommand({0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 35},
                  TriggerMode::RecoilRattle, {0, 1, 120, 35, 0});
    expectCommand({0x26, 0xFC, 0x03, 0, 0, 0, 0, 0, 0, 120},
                  TriggerMode::RecoilRattle, {38, 1, 15, 120, 0});
    expectCommand({0x26, 0, 0x02, 0, 0, 0, 0x38, 0, 0, 255},
                  TriggerMode::RecoilRattle, {173, 1, 120, 255, 0});
    expectCommand({0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 0, 35},
                  TriggerMode::Normal, {});
    expectCommand({0x26, 0, 0, 0, 0, 0, 0, 0, 0, 35}, TriggerMode::Normal, {});

    expectUnsupported({0x21, 0x01, 0x04});
    expectUnsupported({0x25, 0x84, 0x80});
    expectUnsupported({0x26, 0xFF, 0x83});
    expectUnsupported({99});
    expectUnsupported({0});
}
