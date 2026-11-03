#include "dualsense/AdaptiveTriggerTranslation.h"

#include <algorithm>
#include <cmath>

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

std::optional<std::uint8_t> limitedForce(std::uint8_t strength) noexcept {
    constexpr unsigned kLimitedMaximum = 10;
    constexpr unsigned kNativeForceCeiling = 64;
    if (strength > kLimitedMaximum) return std::nullopt;
    return static_cast<std::uint8_t>(
        (strength * kNativeForceCeiling + kLimitedMaximum / 2U) / kLimitedMaximum);
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

ForceTriggerCommand makeBow(TriggerSide side, ZoneRange zones, unsigned drawStrength,
                            unsigned snapForce) {
    constexpr unsigned kLastZone = 9;
    constexpr unsigned kStrongSnap = 7;
    const auto start = strokeForZone(zones.first);
    const auto resistance = static_cast<std::uint8_t>(drawStrength * 8U);
    if (zones.first >= zones.last || zones.last >= kLastZone || snapForce < kStrongSnap) {
        return make(side, TriggerMode::Race, {start, resistance, 0, 0, 0});
    }
    const auto length = static_cast<std::uint8_t>(strokeForZone(zones.last) - start);
    return make(side, TriggerMode::SniperBreak, {start, length, resistance, 0, 0});
}

std::uint32_t packedStrengths(const std::array<std::uint8_t, 11>& effect) noexcept {
    return static_cast<std::uint32_t>(effect[3]) |
           (static_cast<std::uint32_t>(effect[4]) << 8U) |
           (static_cast<std::uint32_t>(effect[5]) << 16U) |
           (static_cast<std::uint32_t>(effect[6]) << 24U);
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
        return makeBow(side, zones, (effect[3] & 0x07U) + 1U, ((effect[3] >> 3U) & 0x07U) + 1U);
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

    const auto packed = packedStrengths(effect);
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

unsigned axisForStroke(unsigned stroke) noexcept {
    return (std::min)(255U, stroke * 255U / 192U);
}

unsigned strokeAtOrAfterAxis(unsigned axis) noexcept {
    return (axis * 192U + 254U) / 255U;
}

} // namespace

std::optional<BowEmulation> describeBowEmulation(
    TriggerSide side, const std::array<std::uint8_t, 11>& effect) {
    constexpr unsigned kLastZone = 9;
    constexpr unsigned kStrongSnap = 7;
    constexpr unsigned kMinimumDrawZones = 4;
    constexpr unsigned kRuptureAxisPercent = 85;
    constexpr unsigned kRearmMargin = 10;
    if (effect[0] != 0x22) return std::nullopt;
    const auto mask = static_cast<unsigned>(effect[1]) |
                      (static_cast<unsigned>(effect[2]) << 8U);
    if (mask == 0 || (mask & ~0x03FFU) != 0) return std::nullopt;
    const auto zones = scanZones(mask);
    const unsigned drawStrength = (effect[3] & 0x07U) + 1U;
    const unsigned snapForce = ((effect[3] >> 3U) & 0x07U) + 1U;
    if (zones.first >= zones.last || zones.last >= kLastZone || snapForce < kStrongSnap ||
        zones.last - zones.first < kMinimumDrawZones) {
        return std::nullopt;
    }
    const auto start = strokeForZone(zones.first);
    const auto end = strokeForZone(zones.last);
    const auto resistance = static_cast<std::uint8_t>(drawStrength * 8U);
    BowEmulation bow;
    bow.draw = make(side, TriggerMode::Race, {start, resistance, 0, 0, 0});
    bow.ruptureAxis = static_cast<std::uint8_t>(axisForStroke(end) * kRuptureAxisPercent / 100U);
    const auto ruptureStart = static_cast<std::uint8_t>((std::clamp)(
        strokeAtOrAfterAxis(bow.ruptureAxis), static_cast<unsigned>(start), end - 1U));
    bow.rupture = make(side, TriggerMode::SniperBreak,
                       {ruptureStart, static_cast<std::uint8_t>(end - ruptureStart), resistance, 0, 0});
    const auto startAxis = axisForStroke(start);
    bow.rearmAxis = static_cast<std::uint8_t>(
        startAxis > 2U * kRearmMargin ? startAxis - kRearmMargin : kRearmMargin);
    return bow;
}

namespace {

unsigned zoneForAxis(unsigned axis) noexcept {
    for (unsigned zone = 9; zone > 0; --zone) {
        if (axis >= axisForStroke(strokeForZone(zone))) return zone;
    }
    return 0;
}

} // namespace

ZoneTracker::ZoneTracker(std::uint8_t axis) noexcept : zone_(zoneForAxis(axis)) {}

unsigned ZoneTracker::update(std::uint8_t axis, unsigned hysteresis) noexcept {
    const auto raised = zoneForAxis(axis);
    if (raised > zone_) {
        zone_ = raised;
    } else {
        const auto lowered = zoneForAxis((std::min)(255U, axis + hysteresis));
        if (lowered < zone_) zone_ = lowered;
    }
    return zone_;
}

std::optional<PeriodicEmulation> describePeriodicEmulation(
    const std::array<std::uint8_t, 11>& effect) {
    if (effect[0] != 0x23 && effect[0] != 0x27) return std::nullopt;
    const auto mask = static_cast<unsigned>(effect[1]) |
                      (static_cast<unsigned>(effect[2]) << 8U);
    if (mask == 0 || (mask & ~0x03FFU) != 0) return std::nullopt;
    const auto zones = scanZones(mask);
    if (zones.first >= zones.last || effect[4] == 0) return std::nullopt;
    PeriodicEmulation emulation;
    emulation.firstZone = zones.first;
    emulation.lastZone = zones.last;
    emulation.frequency = effect[4];
    if (effect[0] == 0x23) {
        emulation.kind = PeriodicEmulation::Kind::Galloping;
        emulation.secondFoot = effect[3] & 0x07U;
        emulation.firstFoot = (effect[3] >> 3U) & 0x07U;
        if (emulation.firstFoot >= emulation.secondFoot ||
            effect[4] > kMaximumGallopingFrequency) {
            return std::nullopt;
        }
        return emulation;
    }
    emulation.kind = PeriodicEmulation::Kind::Machine;
    emulation.amplitudeA = effect[3] & 0x07U;
    emulation.amplitudeB = (effect[3] >> 3U) & 0x07U;
    emulation.periodTenths = effect[5];
    if (emulation.amplitudeA == emulation.amplitudeB ||
        emulation.periodTenths < kMinimumMachinePeriodTenths) {
        return std::nullopt;
    }
    return emulation;
}

ZoneUnits productionZoneUnits() noexcept {
    ZoneUnits units;
    units.forceTable = kPerceptualForceTable;
    units.vibrationTable = kPerceptualVibrationTable;
    return units;
}

ZoneUnits periodicUnitsForPosition(ZoneUnits units, unsigned position) noexcept {
    if (position >= kBottomTriggerPosition) {
        units.gallopingMinimumBeatSeconds = units.gallopingBottomMinimumBeatSeconds;
        units.machineAmplitudeFloor = units.machineBottomAmplitudeFloor;
    }
    return units;
}

ForceTriggerCommand renderPeriodicCommand(
    TriggerSide side, const PeriodicEmulation& emulation, double seconds, bool inRange,
    ZoneUnits units) {
    constexpr unsigned kStartMargin = 5;
    constexpr unsigned kCycleSlots = 8;
    constexpr double kSilenceLevel = 0.35;
    constexpr double kTwoPi = 6.283185307179586;
    if (!inRange || seconds < 0.0) return make(side, TriggerMode::Normal);
    const unsigned boundary = strokeForZone(emulation.firstZone);
    const auto start = static_cast<std::uint8_t>(boundary > kStartMargin ? boundary - kStartMargin : 0U);
    if (emulation.kind == PeriodicEmulation::Kind::Galloping) {
        const double cycleSeconds = 1.0 / emulation.frequency;
        const double slotSeconds = cycleSeconds / kCycleSlots;
        const double cycle = seconds * emulation.frequency;
        const double position = (cycle - std::floor(cycle)) * cycleSeconds;
        const double width = (std::max)(units.gallopingBeatSlots * slotSeconds,
                                        units.gallopingMinimumBeatSeconds);
        const auto beat = [position, width, slotSeconds, cycleSeconds](unsigned foot) {
            const double begin = foot * slotSeconds;
            return (position >= begin && position < begin + width) ||
                   (position + cycleSeconds >= begin && position + cycleSeconds < begin + width);
        };
        if (!beat(emulation.firstFoot) && !beat(emulation.secondFoot)) {
            return make(side, TriggerMode::Normal);
        }
        return make(side, TriggerMode::RecoilRattle,
                    {start, 1, units.gallopingAmplitude, units.gallopingCarrier, 0});
    }
    const double period = emulation.periodTenths / 10.0;
    const double phase = seconds / period - std::floor(seconds / period);
    const double mix = 0.5 - 0.5 * std::cos(kTwoPi * phase);
    const double level = emulation.amplitudeA +
                         (static_cast<double>(emulation.amplitudeB) - emulation.amplitudeA) * mix;
    if (level < kSilenceLevel) return make(side, TriggerMode::Normal);
    const auto tabulated = [&units](unsigned index) {
        const auto entry = units.vibrationTable[index];
        return entry != 0 ? static_cast<double>(entry)
                          : static_cast<double>((index + 1U) * units.vibration);
    };
    const unsigned lower = (std::min)(static_cast<unsigned>(level), 7U);
    const unsigned upper = (std::min)(lower + 1U, 7U);
    const double amplitude = tabulated(lower) +
                             (tabulated(upper) - tabulated(lower)) * (level - lower);
    const long rounded = (std::max)(std::lround(amplitude), static_cast<long>(units.machineAmplitudeFloor));
    return make(side, TriggerMode::RecoilRattle,
                {start, 1,
                 static_cast<std::uint8_t>((std::min)((std::max)(rounded, 1L), 255L)),
                 emulation.frequency, 0});
}

std::optional<ZoneEmulation> describeZoneEmulation(
    const std::array<std::uint8_t, 11>& effect) {
    constexpr unsigned kLastZone = 9;
    if (effect[0] != 0x21 && effect[0] != 0x26) return std::nullopt;
    const bool vibration = effect[0] == 0x26;
    if (vibration && effect[9] < kMinimumEmulatedVibrationFrequency) return std::nullopt;
    const auto mask = static_cast<unsigned>(effect[1]) |
                      (static_cast<unsigned>(effect[2]) << 8U);
    if (mask == 0 || (mask & ~0x03FFU) != 0) return std::nullopt;
    const auto zones = scanZones(mask);
    const auto packed = packedStrengths(effect);
    ZoneEmulation emulation;
    emulation.frequency = vibration ? effect[9] : std::uint8_t{0};
    bool graded = false;
    for (unsigned zone = 0; zone < 10; ++zone) {
        if ((mask & (1U << zone)) == 0) continue;
        emulation.strength[zone] = static_cast<std::uint8_t>(((packed >> (zone * 3U)) & 0x07U) + 1U);
        graded = graded || emulation.strength[zone] != emulation.strength[zones.first];
    }
    const bool contiguous = zones.active == zones.last - zones.first + 1U;
    if (!graded && contiguous && zones.last == kLastZone) return std::nullopt;
    return emulation;
}

ForceTriggerCommand renderZoneCommand(
    TriggerSide side, const ZoneEmulation& emulation, unsigned zone, ZoneUnits units) {
    unsigned target = (std::min)(zone, 9U);
    while (target < 10 && emulation.strength[target] == 0) ++target;
    if (target == 10) return make(side, TriggerMode::Normal);
    constexpr unsigned kStartMargin = 5;
    const auto strength = emulation.strength[target];
    unsigned runStart = target;
    if (emulation.frequency != 0) {
        while (runStart > 0 && emulation.strength[runStart - 1] != 0) --runStart;
    } else {
        while (runStart > 0 && emulation.strength[runStart - 1] == strength) --runStart;
    }
    const unsigned boundary = strokeForZone(runStart);
    const auto start = static_cast<std::uint8_t>(boundary > kStartMargin ? boundary - kStartMargin : 0U);
    if (emulation.frequency != 0) {
        const auto tabulatedAmplitude =
            units.vibrationTable[static_cast<std::size_t>(strength - 1U) % units.vibrationTable.size()];
        const auto amplitude = tabulatedAmplitude != 0
            ? static_cast<unsigned>(tabulatedAmplitude)
            : static_cast<unsigned>(strength) * units.vibration;
        return make(side, TriggerMode::RecoilRattle,
                    {start, 1, static_cast<std::uint8_t>((std::min)((std::max)(amplitude, 1U), 255U)),
                     emulation.frequency, 0});
    }
    const auto tabulated = units.forceTable[static_cast<std::size_t>(strength - 1U) % units.forceTable.size()];
    const auto force = tabulated != 0
        ? tabulated
        : static_cast<std::uint8_t>((std::min)(strength * static_cast<unsigned>(units.force), 255U));
    return make(side, TriggerMode::Race, {start, force, 0, 0, 0});
}

std::optional<ForceTriggerCommand> translateAdaptiveTrigger(
    TriggerSide side, const std::array<std::uint8_t, 11>& effect,
    std::uint8_t) {
    const auto type = effect[0];
    if (type == 1) return make(side, TriggerMode::Race, {effect[1], effect[2], 0, 0, 0});
    if (type == 0x11) {
        const auto force = limitedForce(effect[2]);
        if (!force) return std::nullopt;
        if (*force == 0) return make(side, TriggerMode::Normal);
        return make(side, TriggerMode::Race, {effect[1], *force, 0, 0, 0});
    }
    if (type == 2 || type == 0x12) {
        if (effect[2] < effect[1]) return std::nullopt;
        const auto length = static_cast<std::uint8_t>((std::max)(effect[2] - effect[1], 1));
        if (type == 2) return make(side, TriggerMode::SniperBreak, {effect[1], length, effect[3], 0, 0});
        const auto force = limitedForce(effect[3]);
        if (!force) return std::nullopt;
        if (*force == 0) return make(side, TriggerMode::Normal);
        return make(side, TriggerMode::SniperBreak, {effect[1], length, *force, 0, 0});
    }
    if (type == 5) return make(side, TriggerMode::Normal);
    if (type == 6) return make(side, TriggerMode::RecoilRattle,
                              {effect[3], effect[2], effect[2], effect[1], 0});
    if ((type >= 0x21 && type <= 0x23) || type == 0x25 || type == 0x26 || type == 0x27) {
        return translateNativeTrigger(side, effect);
    }
    return std::nullopt;
}

} // namespace asb::dualsense
