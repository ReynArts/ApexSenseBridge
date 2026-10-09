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

struct ZoneRange {
    unsigned first = 10;
    unsigned last = 0;
    unsigned active = 0;
};

ZoneRange scanZones(unsigned mask) noexcept {
    ZoneRange range;
    for (unsigned zone = 0; zone < 10; ++zone) {
        if ((mask & (1U << zone)) == 0) continue;
        range.first = (std::min)(range.first, zone);
        range.last = zone;
        ++range.active;
    }
    return range;
}

// Single-zone masks widen to a one-zone span; sparse masks use first..last.
ForceTriggerCommand makeBreak(TriggerSide side, ZoneRange zones, unsigned strength) {
    if (zones.first == zones.last) {
        zones.last = (std::min)(zones.first + 1U, 9U);
        zones.first = zones.last - 1U;
    }
    const auto start = strokeForZone(zones.first);
    const auto length = static_cast<std::uint8_t>(strokeForZone(zones.last) - start);
    return make(side, TriggerMode::SniperBreak,
                {start, length, static_cast<std::uint8_t>(strength * 8U), 0, 0});
}

std::optional<ForceTriggerCommand> translateNativeTrigger(
    TriggerSide side, const std::array<std::uint8_t, 11>& effect) {
    const auto mask = static_cast<unsigned>(effect[1]) |
                      (static_cast<unsigned>(effect[2]) << 8U);
    if ((mask & ~0x03FFU) != 0) return std::nullopt;
    if (mask == 0) return make(side, TriggerMode::Normal);

    const auto zones = scanZones(mask);
    const auto start = strokeForZone(zones.first);
    switch (effect[0]) {
    case 0x22:
    case 0x25:
        return makeBreak(side, zones, (effect[3] & 0x07U) + 1U);
    case 0x23:
        if (effect[4] == 0) return make(side, TriggerMode::Normal);
        return make(side, TriggerMode::RecoilRattle, {start, 1, 60, effect[4], 0});
    case 0x27: {
        if (effect[4] == 0) return make(side, TriggerMode::Normal);
        const auto average = ((effect[3] & 0x07U) + ((effect[3] >> 3U) & 0x07U)) / 2U + 1U;
        return make(side, TriggerMode::RecoilRattle,
                    {start, 1, static_cast<std::uint8_t>(average * 10U), effect[4], 0});
    }
    default: break;
    }

    const auto packed = static_cast<std::uint32_t>(effect[3]) |
                        (static_cast<std::uint32_t>(effect[4]) << 8U) |
                        (static_cast<std::uint32_t>(effect[5]) << 16U) |
                        (static_cast<std::uint32_t>(effect[6]) << 24U);
    std::array<unsigned, 10> strengths{};
    unsigned peakStrength = 0;
    for (unsigned zone = 0; zone < 10; ++zone) {
        if ((mask & (1U << zone)) == 0) continue;
        strengths[zone] = ((packed >> (zone * 3U)) & 0x07U) + 1U;
        peakStrength = (std::max)(peakStrength, strengths[zone]);
    }

    if (effect[0] == 0x21) {
        const auto resistance = static_cast<std::uint8_t>(peakStrength * 8U);
        return make(side, TriggerMode::Race, {start, resistance, 0, 0, 0});
    }

    const auto frequency = effect[9];
    if (frequency == 0) return make(side, TriggerMode::Normal);
    // Start where the effect reaches half its peak and average from there, not the peak.
    unsigned first = zones.first;
    while (strengths[first] * 2U < peakStrength) ++first;
    unsigned sum = 0;
    unsigned count = 0;
    for (unsigned zone = first; zone < 10; ++zone) {
        if (strengths[zone] == 0) continue;
        sum += strengths[zone];
        ++count;
    }
    auto amplitude = (sum * 10U + count / 2U) / count;
    if (frequency < 10) amplitude /= 2U;
    return make(side, TriggerMode::RecoilRattle,
                {strokeForZone(first), 1,
                 static_cast<std::uint8_t>((std::max)(amplitude, 1U)), frequency, 0});
}

} // namespace

std::optional<ForceTriggerCommand> translateAdaptiveTrigger(
    TriggerSide side, const std::array<std::uint8_t, 11>& effect,
    std::uint8_t) {
    const auto type = effect[0];
    if (type == 1 || type == 0x11) return make(side, TriggerMode::Race, {effect[1], effect[2], 0, 0, 0});
    if (type == 2 || type == 0x12) return make(side, TriggerMode::SniperBreak, {effect[1], effect[2], effect[3], 0, 0});
    if (type == 5) return make(side, TriggerMode::Normal);
    if (type == 6) return make(side, TriggerMode::RecoilRattle,
                              {effect[3], effect[2], effect[2], effect[1], 0});
    if ((type >= 0x21 && type <= 0x23) || type == 0x25 || type == 0x26 || type == 0x27) {
        return translateNativeTrigger(side, effect);
    }
    return std::nullopt;
}

} // namespace asb::dualsense
