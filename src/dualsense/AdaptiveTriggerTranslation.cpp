#include "dualsense/AdaptiveTriggerTranslation.h"

#include <algorithm>

namespace asb::dualsense {
namespace {

ForceTriggerCommand make(TriggerSide side, TriggerMode mode,
                         std::array<std::uint8_t, 5> params = {}) {
    return ForceTriggerCommand{side, mode, params};
}

std::uint8_t strokeForZone(unsigned zone) noexcept {
    constexpr unsigned kMaximumStroke = 192;
    return static_cast<std::uint8_t>((zone * kMaximumStroke + 5U) / 10U);
}

std::optional<ForceTriggerCommand> translateNativeTrigger(
    TriggerSide side, const std::array<std::uint8_t, 11>& effect) {
    const auto mask = static_cast<unsigned>(effect[1]) |
                      (static_cast<unsigned>(effect[2]) << 8U);
    if ((mask & ~0x03FFU) != 0) return std::nullopt;
    if (mask == 0) return make(side, TriggerMode::Normal);

    unsigned firstZone = 10;
    unsigned lastZone = 0;
    unsigned activeZones = 0;
    for (unsigned zone = 0; zone < 10; ++zone) {
        if ((mask & (1U << zone)) == 0) continue;
        firstZone = (std::min)(firstZone, zone);
        lastZone = zone;
        ++activeZones;
    }

    const auto start = strokeForZone(firstZone);
    if (effect[0] == 0x25) {
        if (activeZones != 2) return std::nullopt;
        const auto strength = static_cast<std::uint8_t>(((effect[3] & 0x07U) + 1U) * 8U);
        const auto length = static_cast<std::uint8_t>(strokeForZone(lastZone) - start);
        return make(side, TriggerMode::SniperBreak,
                    {start, length, strength, 0, 0});
    }

    const auto packed = static_cast<std::uint32_t>(effect[3]) |
                        (static_cast<std::uint32_t>(effect[4]) << 8U) |
                        (static_cast<std::uint32_t>(effect[5]) << 16U) |
                        (static_cast<std::uint32_t>(effect[6]) << 24U);
    unsigned peakStrength = 0;
    for (unsigned zone = 0; zone < 10; ++zone) {
        if ((mask & (1U << zone)) == 0) continue;
        peakStrength = (std::max)(peakStrength, ((packed >> (zone * 3U)) & 0x07U) + 1U);
    }

    if (effect[0] == 0x21) {
        const auto resistance = static_cast<std::uint8_t>(peakStrength * 8U);
        return make(side, TriggerMode::Race, {start, resistance, 0, 0, 0});
    }

    const auto frequency = effect[9];
    if (frequency == 0) return make(side, TriggerMode::Normal);
    const auto strength = static_cast<std::uint8_t>(peakStrength * 15U);
    return make(side, TriggerMode::RecoilRattle, {start, 1, strength, frequency, 0});
}

} // namespace

std::optional<ForceTriggerCommand> translateAdaptiveTrigger(
    TriggerSide side, const std::array<std::uint8_t, 11>& effect,
    std::uint8_t) {
    const auto type = effect[0];
    if (type == 1) return make(side, TriggerMode::Race, {effect[1], effect[2], 0, 0, 0});
    if (type == 2) return make(side, TriggerMode::SniperBreak, {effect[1], effect[2], effect[3], 0, 0});
    if (type == 5) return make(side, TriggerMode::Normal);
    if (type == 6) return make(side, TriggerMode::RecoilRattle,
                              {effect[3], effect[2], effect[2], effect[1], 0});
    if (type == 0x21 || type == 0x25 || type == 0x26) {
        return translateNativeTrigger(side, effect);
    }
    return std::nullopt;
}

} // namespace asb::dualsense
