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
    expectCommand({2, 25, 90, 40}, TriggerMode::SniperBreak, {25, 65, 40, 0, 0});
    expectCommand({2, 40, 40, 40}, TriggerMode::SniperBreak, {40, 1, 40, 0, 0});
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
    expectCommand({0x25, 0x04, 0, 3}, TriggerMode::SniperBreak, {38, 20, 32, 0, 0});
    expectCommand({0x25, 0, 0x02, 3}, TriggerMode::SniperBreak, {154, 19, 32, 0, 0});
    expectCommand({0x25, 0x94}, TriggerMode::SniperBreak, {38, 96, 8, 0, 0});

    expectCommand({0x22, 0x84, 0, 0x2B}, TriggerMode::Race, {38, 32, 0, 0, 0});
    expectCommand({0x22, 0x01, 0x02, 0x39}, TriggerMode::Race, {0, 16, 0, 0, 0});
    expectCommand({0x22, 0x04, 0, 0x07}, TriggerMode::Race, {38, 64, 0, 0, 0});
    expectCommand({0x22}, TriggerMode::Normal, {});
    expectCommand({0x22, 0, 0, 0x38}, TriggerMode::Normal, {});

    expectCommand({0x22, 0x01, 0x02, 0x18}, TriggerMode::Race, {0, 8, 0, 0, 0});
    expectCommand({0x22, 0x01, 0x01, 0x38}, TriggerMode::SniperBreak, {0, 154, 8, 0, 0});
    expectCommand({0x22, 0x01, 0x02, 0x39}, TriggerMode::Race, {0, 16, 0, 0, 0});
    expectCommand({0x22, 0x80, 0x02, 0x08}, TriggerMode::Race, {134, 8, 0, 0, 0});
    expectCommand({0x22, 0x40, 0x01, 0x21}, TriggerMode::Race, {115, 16, 0, 0, 0});
    expectCommand({0x22, 0x04, 0x02, 0x32}, TriggerMode::Race, {38, 24, 0, 0, 0});
    expectCommand({0x22, 0x04, 0x01, 0x3A}, TriggerMode::SniperBreak, {38, 116, 24, 0, 0});
    expectCommand({0x21, 0x80, 0x03, 0, 0, 0, 0, 0, 0, 0, 0}, TriggerMode::Race, {134, 8, 0, 0, 0});

    expectCommand({0x22, 0x01, 0x01, 0x30}, TriggerMode::SniperBreak, {0, 154, 8, 0, 0});
    expectCommand({0x22, 0x01, 0x01, 0x28}, TriggerMode::Race, {0, 8, 0, 0, 0});
    expectCommand({0x22, 0x01, 0x01, 0x3F}, TriggerMode::SniperBreak, {0, 154, 64, 0, 0});
    expectCommand({0x22, 0x01, 0x01, 0x07}, TriggerMode::Race, {0, 64, 0, 0, 0});
    expectCommand({0x22, 0x01, 0x02, 0x3F}, TriggerMode::Race, {0, 64, 0, 0, 0});
    expectCommand({0x22, 0x08, 0x01, 0x38}, TriggerMode::SniperBreak, {58, 96, 8, 0, 0});
    expectCommand({0x22, 0x18, 0, 0x38}, TriggerMode::SniperBreak, {58, 19, 8, 0, 0});
    expectCommand({0x22, 0x80, 0x01, 0x30}, TriggerMode::SniperBreak, {134, 20, 8, 0, 0});
    expectCommand({0x22, 0x00, 0x03, 0x38}, TriggerMode::Race, {154, 8, 0, 0, 0});
    expectCommand({0x22, 0x01, 0x01, 0xF8}, TriggerMode::SniperBreak, {0, 154, 8, 0, 0});

    expectCommand({0x22, 0x04, 0, 0x38}, TriggerMode::Race, {38, 8, 0, 0, 0});
    expectCommand({0x22, 0x00, 0x02, 0x38}, TriggerMode::Race, {173, 8, 0, 0, 0});
    expectCommand({0x22, 0x52, 0, 0x38}, TriggerMode::SniperBreak, {19, 96, 8, 0, 0});
    expectCommand({0x22, 0x52, 0x02, 0x38}, TriggerMode::Race, {19, 8, 0, 0, 0});
    expectCommand({0x22, 0xFF, 0x00, 0x38}, TriggerMode::SniperBreak, {0, 134, 8, 0, 0});
    expectCommand({0x22, 0xFF, 0x03, 0x38}, TriggerMode::Race, {0, 8, 0, 0, 0});

    for (unsigned startZone = 0; startZone < 9; ++startZone) {
        for (unsigned endZone = startZone + 1; endZone < 10; ++endZone) {
            const auto mask = (1U << startZone) | (1U << endZone);
            const auto startStroke = (startZone * 192U + 5U) / 10U;
            const auto endStroke = (endZone * 192U + 5U) / 10U;
            for (unsigned snap = 1; snap <= 8; ++snap) {
                for (unsigned draw = 1; draw <= 8; ++draw) {
                    const auto force = static_cast<std::uint8_t>((snap - 1U) << 3U | (draw - 1U));
                    const std::array<std::uint8_t, 11> effect{
                        0x22, static_cast<std::uint8_t>(mask & 0xFFU),
                        static_cast<std::uint8_t>(mask >> 8U), force};
                    const auto resistance = static_cast<std::uint8_t>(draw * 8U);
                    if (endZone < 9 && snap >= 7) {
                        expectCommand(effect, TriggerMode::SniperBreak,
                                      {static_cast<std::uint8_t>(startStroke),
                                       static_cast<std::uint8_t>(endStroke - startStroke),
                                       resistance, 0, 0});
                    } else {
                        expectCommand(effect, TriggerMode::Race,
                                      {static_cast<std::uint8_t>(startStroke), resistance, 0, 0, 0});
                    }
                }
            }
        }
    }

    expectCommand({0x23, 0x04, 0x02, 0x21, 20}, TriggerMode::RecoilRattle, {38, 1, 60, 20, 0});
    expectCommand({0x23, 0x04, 0x02, 0x21, 0}, TriggerMode::Normal, {});
    expectCommand({0x27, 0x03, 0, 0x3B, 30, 5}, TriggerMode::RecoilRattle, {0, 1, 60, 30, 0});
    expectCommand({0x27, 0x03, 0, 0x3B, 0, 5}, TriggerMode::Normal, {});
    expectCommand({0x11, 25, 10}, TriggerMode::Race, {25, 64, 0, 0, 0});
    expectCommand({0x11, 25, 5}, TriggerMode::Race, {25, 32, 0, 0, 0});
    expectCommand({0x11, 25, 1}, TriggerMode::Race, {25, 6, 0, 0, 0});
    expectCommand({0x11, 25, 0}, TriggerMode::Normal, {});
    expectCommand({0x12, 25, 90, 10}, TriggerMode::SniperBreak, {25, 65, 64, 0, 0});
    expectCommand({0x12, 16, 116, 5}, TriggerMode::SniperBreak, {16, 100, 32, 0, 0});
    expectCommand({0x12, 40, 40, 5}, TriggerMode::SniperBreak, {40, 1, 32, 0, 0});
    expectCommand({0x12, 25, 90, 0}, TriggerMode::Normal, {});

    expectCommand({0x26, 0xF0, 0x03, 0, 0, 0, 0, 0, 0, 35},
                  TriggerMode::RecoilRattle, {77, 1, 10, 35, 0});
    expectCommand({0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 35},
                  TriggerMode::RecoilRattle, {0, 1, 80, 35, 0});
    expectCommand({0x26, 0xFC, 0x03, 0, 0, 0, 0, 0, 0, 120},
                  TriggerMode::RecoilRattle, {38, 1, 10, 120, 0});
    expectCommand({0x26, 0, 0x02, 0, 0, 0, 0x38, 0, 0, 255},
                  TriggerMode::RecoilRattle, {173, 1, 80, 255, 0});
    // Horizon held bow: low uniform amplitude at 4 Hz must stay a gentle pulse.
    expectCommand({0x26, 0xFF, 0x03, 0x49, 0x92, 0x24, 0x09, 0, 0, 4},
                  TriggerMode::RecoilRattle, {0, 1, 10, 4, 0});
    // Graded ramp starts where it reaches half of peak and averages from there.
    expectCommand({0x26, 0xFF, 0x03, 0x40, 0x22, 0x6D, 0x24, 0, 0, 35},
                  TriggerMode::RecoilRattle, {77, 1, 40, 35, 0});
    expectCommand({0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 0, 35},
                  TriggerMode::Normal, {});
    expectCommand({0x26, 0, 0, 0, 0, 0, 0, 0, 0, 35}, TriggerMode::Normal, {});

    {
        const auto bow = asb::dualsense::describeBowEmulation(
            asb::TriggerSide::Right, {0x22, 0x44, 0x00, 0x3B});
        assert(bow);
        assert(bow->draw.side == asb::TriggerSide::Right && bow->rupture.side == asb::TriggerSide::Right);
        assert(bow->draw.mode == TriggerMode::Race);
        assert((bow->draw.params == std::array<std::uint8_t, 5>{38, 32, 0, 0, 0}));
        assert(bow->rupture.mode == TriggerMode::SniperBreak);
        assert((bow->rupture.params == std::array<std::uint8_t, 5>{98, 17, 32, 0, 0}));
        assert(bow->rupture.params[0] + bow->rupture.params[1] == 115);
        assert(bow->ruptureAxis == 129 && bow->rearmAxis == 40);
        assert(bow->rearmAxis < bow->ruptureAxis);
    }
    {
        const auto control = asb::dualsense::describeBowEmulation(
            asb::TriggerSide::Right, {0x22, 0x01, 0x01, 0x38});
        assert(control);
        assert((control->draw.params == std::array<std::uint8_t, 5>{0, 8, 0, 0, 0}));
        assert(control->rupture.mode == TriggerMode::SniperBreak);
        assert((control->rupture.params == std::array<std::uint8_t, 5>{131, 23, 8, 0, 0}));
        assert(control->rupture.params[0] + control->rupture.params[1] == 154);
        assert(control->ruptureAxis == 173 && control->rearmAxis == 10);
    }
    for (unsigned startZone = 0; startZone < 9; ++startZone) {
        for (unsigned endZone = startZone + 1; endZone < 9; ++endZone) {
            const auto mask = (1U << startZone) | (1U << endZone);
            const auto startStroke = (startZone * 192U + 5U) / 10U;
            const auto endStroke = (endZone * 192U + 5U) / 10U;
            for (unsigned draw = 1; draw <= 8; ++draw) {
                const std::array<std::uint8_t, 11> effect{
                    0x22, static_cast<std::uint8_t>(mask & 0xFFU), static_cast<std::uint8_t>(mask >> 8U),
                    static_cast<std::uint8_t>(0x38U | (draw - 1U))};
                const auto bow = asb::dualsense::describeBowEmulation(asb::TriggerSide::Left, effect);
                if (endZone - startZone < 4) {
                    assert(!bow);
                    continue;
                }
                assert(bow && bow->rearmAxis < bow->ruptureAxis);
                const unsigned ruptureStart = bow->rupture.params[0];
                const unsigned ruptureLength = bow->rupture.params[1];
                assert(ruptureStart >= startStroke && ruptureLength >= 1U);
                assert(ruptureStart + ruptureLength == endStroke);
                assert(bow->rupture.params[2] == draw * 8U && bow->draw.params[1] == draw * 8U);
                assert(ruptureStart * 255U / 192U >= bow->ruptureAxis);
                assert(bow->ruptureAxis * 192U / 255U + 2U >= ruptureStart);
            }
        }
    }
    expectCommand({0x22, 0x09, 0x00, 0x39}, TriggerMode::SniperBreak, {0, 58, 16, 0, 0});
    assert(!asb::dualsense::describeBowEmulation(asb::TriggerSide::Right, {0x22, 0x09, 0x00, 0x39}));
    assert(!asb::dualsense::describeBowEmulation(asb::TriggerSide::Left, {0x22, 0x03, 0x00, 0x3F}));

    {
        constexpr std::array<std::uint8_t, 11> kSpiderManZone9{0x21, 0x00, 0x02};
        constexpr std::array<std::uint8_t, 11> kSpiderManVibration{
            0x26, 0xFF, 0x03, 0x49, 0x92, 0x24, 0x09, 0x00, 0x00, 0xB4, 0x00};
        constexpr std::array<std::uint8_t, 11> kSpiderManZones6To9{0x21, 0xC0, 0x03};
        constexpr std::array<std::uint8_t, 11> kFirstLightRacing{0x21, 0x80, 0x03};
        expectCommand(kSpiderManZone9, TriggerMode::Race, {173, 8, 0, 0, 0});
        expectCommand(kSpiderManVibration, TriggerMode::RecoilRattle, {0, 1, 20, 180, 0});
        expectCommand(kSpiderManZones6To9, TriggerMode::Race, {115, 8, 0, 0, 0});
        expectCommand(kFirstLightRacing, TriggerMode::Race, {134, 8, 0, 0, 0});
        for (const auto& effect : {kSpiderManZone9, kSpiderManVibration, kSpiderManZones6To9, kFirstLightRacing}) {
            assert(!asb::dualsense::describeZoneEmulation(effect));
            assert(!asb::dualsense::describeBowEmulation(asb::TriggerSide::Right, effect));
            assert(!asb::dualsense::describePeriodicEmulation(effect));
        }
    }
    assert(!asb::dualsense::describeBowEmulation(asb::TriggerSide::Left, {0x22, 0x44, 0x00, 0x0B}));
    assert(!asb::dualsense::describeBowEmulation(asb::TriggerSide::Left, {0x22, 0x04, 0x00, 0x3B}));
    assert(!asb::dualsense::describeBowEmulation(asb::TriggerSide::Left, {0x22, 0x44, 0x02, 0x3B}));
    assert(!asb::dualsense::describeBowEmulation(asb::TriggerSide::Left, {0x22, 0x44, 0x04, 0x3B}));
    assert(!asb::dualsense::describeBowEmulation(asb::TriggerSide::Left, {0x22}));
    assert(!asb::dualsense::describeBowEmulation(asb::TriggerSide::Left, {0x25, 0x44, 0x00, 0x3B}));

    {
        using asb::dualsense::describeZoneEmulation;
        using asb::dualsense::renderZoneCommand;
        using asb::dualsense::ZoneEmulation;
        using asb::dualsense::ZoneTracker;
        using Params = std::array<std::uint8_t, 5>;
        const auto expectZone = [](const ZoneEmulation& emulation, unsigned zone,
                                   TriggerMode mode, const Params& params) {
            for (const auto side : {asb::TriggerSide::Left, asb::TriggerSide::Right}) {
                const auto command = renderZoneCommand(side, emulation, zone);
                assert(command.side == side && command.mode == mode && command.params == params);
            }
        };

        const auto ramp = describeZoneEmulation({0x21, 0xFE, 0x03, 0x40, 0xB4, 0xB1, 0x3E});
        assert(ramp);
        assert((ramp->strength == std::array<std::uint8_t, 10>{0, 1, 2, 3, 4, 4, 5, 6, 7, 8}));
        expectZone(*ramp, 0, TriggerMode::Race, {14, 8, 0, 0, 0});
        expectZone(*ramp, 1, TriggerMode::Race, {14, 8, 0, 0, 0});
        expectZone(*ramp, 2, TriggerMode::Race, {33, 16, 0, 0, 0});
        expectZone(*ramp, 3, TriggerMode::Race, {53, 24, 0, 0, 0});
        expectZone(*ramp, 4, TriggerMode::Race, {72, 32, 0, 0, 0});
        expectZone(*ramp, 5, TriggerMode::Race, {72, 32, 0, 0, 0});
        expectZone(*ramp, 6, TriggerMode::Race, {110, 40, 0, 0, 0});
        expectZone(*ramp, 7, TriggerMode::Race, {129, 48, 0, 0, 0});
        expectZone(*ramp, 8, TriggerMode::Race, {149, 56, 0, 0, 0});
        expectZone(*ramp, 9, TriggerMode::Race, {168, 64, 0, 0, 0});

        const auto wall = describeZoneEmulation({0x21, 0x1C, 0x00, 0xC0, 0x7F});
        assert(wall);
        for (unsigned zone = 0; zone <= 4; ++zone) {
            expectZone(*wall, zone, TriggerMode::Race, {33, 64, 0, 0, 0});
        }
        for (unsigned zone = 5; zone <= 9; ++zone) {
            expectZone(*wall, zone, TriggerMode::Normal, {});
        }

        const auto gap = describeZoneEmulation({0x21, 0xC6, 0x03, 0x90, 0x00, 0xFC, 0x3F});
        assert(gap);
        for (unsigned zone = 0; zone <= 2; ++zone) {
            expectZone(*gap, zone, TriggerMode::Race, {14, 24, 0, 0, 0});
        }
        for (unsigned zone = 3; zone <= 9; ++zone) {
            expectZone(*gap, zone, TriggerMode::Race, {110, 64, 0, 0, 0});
        }

        const auto shortRange = describeZoneEmulation({0x21, 0x3C, 0x00});
        assert(shortRange);
        expectZone(*shortRange, 3, TriggerMode::Race, {33, 8, 0, 0, 0});
        expectZone(*shortRange, 6, TriggerMode::Normal, {});
        assert(describeZoneEmulation({0x21, 0x05, 0x00}));

        const auto ends = describeZoneEmulation({0x21, 0x01, 0x02});
        assert(ends);
        expectZone(*ends, 0, TriggerMode::Race, {0, 8, 0, 0, 0});
        expectZone(*ends, 1, TriggerMode::Race, {168, 8, 0, 0, 0});
        expectZone(*ends, 9, TriggerMode::Race, {168, 8, 0, 0, 0});

        const auto vibrationRamp = describeZoneEmulation(
            {0x26, 0xFE, 0x03, 0x40, 0xB4, 0xB1, 0x3E, 0, 0, 30});
        assert(vibrationRamp && vibrationRamp->frequency == 30);
        assert(vibrationRamp->strength == ramp->strength);
        expectZone(*vibrationRamp, 0, TriggerMode::RecoilRattle, {14, 1, 10, 30, 0});
        expectZone(*vibrationRamp, 2, TriggerMode::RecoilRattle, {14, 1, 20, 30, 0});
        expectZone(*vibrationRamp, 4, TriggerMode::RecoilRattle, {14, 1, 40, 30, 0});
        expectZone(*vibrationRamp, 5, TriggerMode::RecoilRattle, {14, 1, 40, 30, 0});
        expectZone(*vibrationRamp, 9, TriggerMode::RecoilRattle, {14, 1, 80, 30, 0});
        assert(!describeZoneEmulation({0x26, 0xFE, 0x03, 0x40, 0xB4, 0xB1, 0x3E, 0, 0, 4}));
        assert(!describeZoneEmulation({0x26, 0xFE, 0x03, 0x40, 0xB4, 0xB1, 0x3E, 0, 0, 15}));
        assert(!describeZoneEmulation({0x26, 0xFE, 0x03, 0x40, 0xB4, 0xB1, 0x3E, 0, 0, 24}));
        assert(describeZoneEmulation({0x26, 0xFE, 0x03, 0x40, 0xB4, 0xB1, 0x3E, 0, 0, 25}));
        assert(describeZoneEmulation({0x26, 0xFE, 0x03, 0x40, 0xB4, 0xB1, 0x3E, 0, 0, 45}));
        assert(describeZoneEmulation({0x26, 0xFE, 0x03, 0x40, 0xB4, 0xB1, 0x3E, 0, 0, 60}));
        const auto shortVibration = describeZoneEmulation({0x26, 0x3C, 0x00, 0, 0, 0, 0, 0, 0, 30});
        assert(shortVibration);
        expectZone(*shortVibration, 3, TriggerMode::RecoilRattle, {33, 1, 10, 30, 0});
        expectZone(*shortVibration, 6, TriggerMode::Normal, {});
        assert(!describeZoneEmulation({0x26, 0xFE, 0x03, 0x40, 0xB4, 0xB1, 0x3E, 0, 0, 0}));
        assert(!describeZoneEmulation({0x26, 0xFF, 0x03, 0, 0, 0, 0, 0, 0, 30}));
        assert(!describeZoneEmulation({0x26, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 30}));

        const asb::dualsense::ZoneUnits wide{16, 20};
        const auto wideRace = renderZoneCommand(asb::TriggerSide::Right, *ramp, 2, wide);
        assert(wideRace.mode == TriggerMode::Race && (wideRace.params == Params{33, 32, 0, 0, 0}));
        assert(renderZoneCommand(asb::TriggerSide::Right, *ramp, 9, wide).params == (Params{168, 128, 0, 0, 0}));
        assert(renderZoneCommand(asb::TriggerSide::Right, *vibrationRamp, 0, wide).params == (Params{14, 1, 20, 30, 0}));
        assert(renderZoneCommand(asb::TriggerSide::Right, *vibrationRamp, 9, wide).params == (Params{14, 1, 160, 30, 0}));
        assert(renderZoneCommand(asb::TriggerSide::Right, *ramp, 9, {40, 10}).params == (Params{168, 255, 0, 0, 0}));
        assert(renderZoneCommand(asb::TriggerSide::Right, *vibrationRamp, 9, {8, 40}).params == (Params{14, 1, 255, 30, 0}));

        asb::dualsense::ZoneUnits perceptual;
        perceptual.forceTable = asb::dualsense::kPerceptualForceTable;
        const auto perceptualAt = [&](unsigned zone) {
            return renderZoneCommand(asb::TriggerSide::Right, *ramp, zone, perceptual).params;
        };
        assert(perceptualAt(0) == (Params{14, 1, 0, 0, 0}));
        assert(perceptualAt(2) == (Params{33, 16, 0, 0, 0}));
        assert(perceptualAt(3) == (Params{53, 32, 0, 0, 0}));
        assert(perceptualAt(4) == (Params{72, 64, 0, 0, 0}));
        assert(perceptualAt(5) == (Params{72, 64, 0, 0, 0}));
        assert(perceptualAt(6) == (Params{110, 96, 0, 0, 0}));
        assert(perceptualAt(7) == (Params{129, 128, 0, 0, 0}));
        assert(perceptualAt(8) == (Params{149, 160, 0, 0, 0}));
        assert(perceptualAt(9) == (Params{168, 192, 0, 0, 0}));
        assert(renderZoneCommand(asb::TriggerSide::Right, *vibrationRamp, 9, perceptual).params ==
               (Params{14, 1, 80, 30, 0}));
        asb::dualsense::ZoneUnits softStart;
        softStart.vibrationTable = {4, 20, 30, 40, 50, 60, 70, 80};
        assert(renderZoneCommand(asb::TriggerSide::Right, *vibrationRamp, 0, softStart).params ==
               (Params{14, 1, 4, 30, 0}));
        assert(renderZoneCommand(asb::TriggerSide::Right, *vibrationRamp, 2, softStart).params ==
               (Params{14, 1, 20, 30, 0}));
        assert(renderZoneCommand(asb::TriggerSide::Right, *vibrationRamp, 9, softStart).params ==
               (Params{14, 1, 80, 30, 0}));
        assert(renderZoneCommand(asb::TriggerSide::Right, *ramp, 2, softStart).params ==
               (Params{33, 16, 0, 0, 0}));

        asb::dualsense::ZoneUnits validated;
        validated.forceTable = asb::dualsense::kPerceptualForceTable;
        validated.vibrationTable = asb::dualsense::kPerceptualVibrationTable;
        const auto validatedVibration = [&](unsigned zone) {
            return renderZoneCommand(asb::TriggerSide::Right, *vibrationRamp, zone, validated).params;
        };
        assert(validatedVibration(0) == (Params{14, 1, 1, 30, 0}));
        assert(validatedVibration(1) == (Params{14, 1, 1, 30, 0}));
        assert(validatedVibration(2) == (Params{14, 1, 15, 30, 0}));
        assert(validatedVibration(3) == (Params{14, 1, 25, 30, 0}));
        assert(validatedVibration(4) == (Params{14, 1, 35, 30, 0}));
        assert(validatedVibration(5) == (Params{14, 1, 35, 30, 0}));
        assert(validatedVibration(6) == (Params{14, 1, 45, 30, 0}));
        assert(validatedVibration(7) == (Params{14, 1, 55, 30, 0}));
        assert(validatedVibration(8) == (Params{14, 1, 65, 30, 0}));
        assert(validatedVibration(9) == (Params{14, 1, 80, 30, 0}));
        const auto production = asb::dualsense::productionZoneUnits();
        assert(production.forceTable == asb::dualsense::kPerceptualForceTable);
        assert(production.vibrationTable == asb::dualsense::kPerceptualVibrationTable);
        assert(production.machineAmplitudeFloor == 40 && production.gallopingMinimumBeatSeconds == 0.15);
        asb::dualsense::ZoneUnits partial;
        partial.forceTable[7] = 200;
        assert(renderZoneCommand(asb::TriggerSide::Right, *ramp, 9, partial).params == (Params{168, 200, 0, 0, 0}));
        assert(renderZoneCommand(asb::TriggerSide::Right, *ramp, 2, partial).params == (Params{33, 16, 0, 0, 0}));

        assert(!describeZoneEmulation({0x21, 0xFC, 0x03}));
        assert(!describeZoneEmulation({0x21, 0x00, 0x02, 0, 0, 0, 0x38}));
        assert(!describeZoneEmulation({0x21, 0x00, 0x00}));
        assert(!describeZoneEmulation({0x21, 0x00, 0x04}));
        assert(!describeZoneEmulation({0x26, 0xFE, 0x03, 0x40, 0xB4, 0xB1, 0x3E}));
        assert(!describeZoneEmulation({0x22, 0x44, 0x00, 0x3B}));
        assert(!describeZoneEmulation({0x11, 38, 5}));

        ZoneTracker tracker;
        assert(tracker.zone() == 0);
        assert(tracker.update(24) == 0);
        assert(tracker.update(25) == 1);
        assert(tracker.update(50) == 2);
        assert(tracker.update(49) == 2);
        assert(tracker.update(44) == 2);
        assert(tracker.update(43) == 1);
        assert(tracker.update(255) == 9);
        assert(tracker.update(229) == 9);
        assert(tracker.update(223) == 9);
        assert(tracker.update(222) == 8);
        assert(tracker.update(0) == 0);
        ZoneTracker steady(60);
        assert(steady.zone() == 2);
        assert(steady.update(40, asb::dualsense::kVibrationZoneHysteresis) == 2);
        assert(steady.update(25, asb::dualsense::kVibrationZoneHysteresis) == 2);
        assert(steady.update(19, asb::dualsense::kVibrationZoneHysteresis) == 1);
        assert(steady.update(52, asb::dualsense::kVibrationZoneHysteresis) == 2);
        ZoneTracker lenient(60);
        assert(lenient.update(40) == 1);
        assert(ZoneTracker(228).zone() == 8 && ZoneTracker(229).zone() == 9);
        assert(ZoneTracker(0).zone() == 0 && ZoneTracker(255).zone() == 9);
    }

    {
        using asb::dualsense::describePeriodicEmulation;
        using asb::dualsense::renderPeriodicCommand;
        using asb::dualsense::PeriodicEmulation;
        using Params = std::array<std::uint8_t, 5>;
        const auto right = asb::TriggerSide::Right;

        const auto heartbeat = describePeriodicEmulation({0x23, 0x01, 0x02, 0x0B, 1});
        assert(heartbeat && heartbeat->kind == PeriodicEmulation::Kind::Galloping);
        assert(heartbeat->firstZone == 0 && heartbeat->lastZone == 9);
        assert(heartbeat->firstFoot == 1 && heartbeat->secondFoot == 3 && heartbeat->frequency == 1);
        const auto horse = describePeriodicEmulation({0x23, 0x01, 0x02, 0x13, 2});
        assert(horse && horse->firstFoot == 2 && horse->secondFoot == 3 && horse->frequency == 2);
        const auto burst = describePeriodicEmulation({0x27, 0x02, 0x02, 0x07, 18, 12});
        assert(burst && burst->kind == PeriodicEmulation::Kind::Machine);
        assert(burst->firstZone == 1 && burst->lastZone == 9);
        assert(burst->amplitudeA == 7 && burst->amplitudeB == 0);
        assert(burst->frequency == 18 && burst->periodTenths == 12);

        assert(!describePeriodicEmulation({0x27, 0x02, 0x02, 0x3F, 9, 1}));
        assert(!describePeriodicEmulation({0x27, 0x02, 0x02, 0x07, 18, 3}));
        assert(describePeriodicEmulation({0x27, 0x02, 0x02, 0x07, 18, 4}));
        assert(!describePeriodicEmulation({0x27, 0x02, 0x02, 0x07, 0, 12}));
        assert(!describePeriodicEmulation({0x23, 0x01, 0x02, 0x0B, 0}));
        assert(describePeriodicEmulation({0x23, 0x01, 0x02, 0x0B, 8}));
        assert(!describePeriodicEmulation({0x23, 0x01, 0x02, 0x0B, 9}));
        assert(!describePeriodicEmulation({0x23, 0x01, 0x02, 0x19, 2}));
        assert(!describePeriodicEmulation({0x23, 0x01, 0x00, 0x0B, 2}));
        assert(!describePeriodicEmulation({0x23, 0x00, 0x00, 0x0B, 2}));
        assert(!describePeriodicEmulation({0x23, 0x01, 0x04, 0x0B, 2}));
        assert(!describePeriodicEmulation({0x21, 0x01, 0x02, 0x0B, 2}));
        assert(!describePeriodicEmulation({0x26, 0x01, 0x02, 0x0B, 2}));

        const auto at = [&](const PeriodicEmulation& emulation, double seconds, bool inRange = true) {
            return renderPeriodicCommand(right, emulation, seconds, inRange);
        };
        const Params beat{0, 1, 60, 30, 0};
        assert(at(*heartbeat, 0.00).mode == TriggerMode::Normal);
        assert(at(*heartbeat, 0.10).mode == TriggerMode::Normal);
        assert(at(*heartbeat, 0.15).mode == TriggerMode::RecoilRattle && at(*heartbeat, 0.15).params == beat);
        assert(at(*heartbeat, 0.25).mode == TriggerMode::RecoilRattle && at(*heartbeat, 0.30).mode == TriggerMode::Normal);
        assert(at(*heartbeat, 0.40).mode == TriggerMode::RecoilRattle);
        assert(at(*heartbeat, 0.50).mode == TriggerMode::RecoilRattle && at(*heartbeat, 0.55).mode == TriggerMode::Normal);
        assert(at(*heartbeat, 0.90).mode == TriggerMode::Normal);
        assert(at(*heartbeat, 1.15).mode == TriggerMode::RecoilRattle);
        assert(at(*heartbeat, 1.40).mode == TriggerMode::RecoilRattle);
        assert(at(*heartbeat, 1.60).mode == TriggerMode::Normal);
        assert(at(*heartbeat, 0.15, false).mode == TriggerMode::Normal);
        assert(at(*heartbeat, -1.0).mode == TriggerMode::Normal);
        assert(at(*horse, 0.14).mode == TriggerMode::RecoilRattle);
        assert(at(*horse, 0.17).mode == TriggerMode::RecoilRattle);
        assert(at(*horse, 0.20).mode == TriggerMode::RecoilRattle);
        assert(at(*horse, 0.23).mode == TriggerMode::RecoilRattle && at(*horse, 0.30).mode == TriggerMode::RecoilRattle);
        assert(at(*horse, 0.35).mode == TriggerMode::Normal);
        assert(at(*horse, 0.64).mode == TriggerMode::RecoilRattle);
        assert(at(*horse, 0.40).mode == TriggerMode::Normal);

        assert(at(*burst, 0.0).params == (Params{14, 1, 80, 18, 0}));
        assert(at(*burst, 0.3).params == (Params{14, 1, 45, 18, 0}));
        assert(at(*burst, 0.6).mode == TriggerMode::Normal);
        assert(at(*burst, 0.9).mode == TriggerMode::RecoilRattle);
        assert(at(*burst, 1.2).params == (Params{14, 1, 80, 18, 0}));
        assert(at(*burst, 0.3, false).mode == TriggerMode::Normal);
        assert(at(*burst, 0.5).mode == TriggerMode::RecoilRattle && at(*burst, 0.5).params[2] == 40);
        {
            const auto fullyPressed = asb::dualsense::periodicUnitsForPosition(asb::dualsense::ZoneUnits{}, 250);
            const auto soft = renderPeriodicCommand(right, *burst, 0.5, true, fullyPressed);
            assert(soft.mode == TriggerMode::RecoilRattle && soft.params[2] > 10 && soft.params[2] < 20);
            asb::dualsense::ZoneUnits noFloor;
            noFloor.machineAmplitudeFloor = 0;
            assert(renderPeriodicCommand(right, *burst, 0.5, true, noFloor).params[2] == soft.params[2]);
            assert(renderPeriodicCommand(right, *burst, 0.6, true, noFloor).mode == TriggerMode::Normal);
            assert(at(*burst, 0.6).mode == TriggerMode::Normal);
        }
        {
            const asb::dualsense::ZoneUnits base;
            const auto mid = asb::dualsense::periodicUnitsForPosition(base, 150);
            const auto edge = asb::dualsense::periodicUnitsForPosition(base, asb::dualsense::kBottomTriggerPosition - 1);
            const auto bottom = asb::dualsense::periodicUnitsForPosition(base, asb::dualsense::kBottomTriggerPosition);
            const auto pressed = asb::dualsense::periodicUnitsForPosition(base, 255);
            assert(mid.gallopingMinimumBeatSeconds == 0.15 && edge.gallopingMinimumBeatSeconds == 0.15);
            assert(bottom.gallopingMinimumBeatSeconds == 0.04 && pressed.gallopingMinimumBeatSeconds == 0.04);
            assert(at(*horse, 0.17).mode == TriggerMode::RecoilRattle);
            assert(renderPeriodicCommand(right, *horse, 0.17, true, bottom).mode == TriggerMode::Normal);
            assert(renderPeriodicCommand(right, *horse, 0.14, true, bottom).mode == TriggerMode::RecoilRattle);
            assert(renderPeriodicCommand(right, *horse, 0.20, true, bottom).mode == TriggerMode::RecoilRattle);
            assert(renderPeriodicCommand(right, *horse, 0.23, true, bottom).mode == TriggerMode::Normal);
        }
        asb::dualsense::ZoneUnits wideBeat;
        wideBeat.gallopingBeatSlots = 1.0;
        wideBeat.gallopingAmplitude = 100;
        assert(at(*heartbeat, 0.30).mode == TriggerMode::Normal);
        assert(renderPeriodicCommand(right, *heartbeat, 0.24, true, wideBeat).params == (Params{0, 1, 100, 30, 0}));
        assert(renderPeriodicCommand(right, *heartbeat, 0.30, true, wideBeat).mode == TriggerMode::Normal);
        wideBeat.gallopingCarrier = 60;
        assert(renderPeriodicCommand(right, *heartbeat, 0.24, true, wideBeat).params == (Params{0, 1, 100, 60, 0}));
        asb::dualsense::ZoneUnits tuned;
        tuned.vibrationTable = asb::dualsense::kPerceptualVibrationTable;
        assert(renderPeriodicCommand(right, *burst, 0.0, true, tuned).params == (Params{14, 1, 80, 18, 0}));
        assert(renderPeriodicCommand(right, *burst, 0.3, true, tuned).params == (Params{14, 1, 40, 18, 0}));
    }

    expectUnsupported({0x21, 0x01, 0x04});
    expectUnsupported({0x25, 0x84, 0x80});
    expectUnsupported({0x26, 0xFF, 0x83});
    expectUnsupported({99});
    expectUnsupported({0xFC});
    expectUnsupported({0x22, 0xFF, 0x83});
    expectUnsupported({0x11, 25, 11});
    expectUnsupported({0x11, 25, 40});
    expectUnsupported({0x12, 25, 90, 11});
    expectUnsupported({0x12, 90, 25, 5});
    expectUnsupported({2, 90, 25, 40});
    expectUnsupported({0});
}
