#include "dualsense/Apex6TriggerEffect.h"

#include "dualsense/AdaptiveTriggerTranslation.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>

namespace asb::dualsense {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint8_t kMaximumDrive = kApex6TriggerMaximumDrive;
constexpr std::uint8_t kNativeDrive = kApex6TriggerNativeMaximumDrive;
constexpr unsigned kMaximumBurstDrive = 255;
constexpr unsigned kZoneCount = 10;
constexpr double kZonesPerPosition = 10.0 / 256.0;
constexpr unsigned kAxisRange = 255;
constexpr unsigned kStrokeRange = 192;
constexpr double kDefaultLegacyFrequency = 85.0;

std::uint8_t positionFor(std::uint8_t zone) noexcept {
    return static_cast<std::uint8_t>((static_cast<unsigned>(zone) * 256U + 9U) / 10U);
}

double axisForStroke(unsigned stroke) noexcept {
    return static_cast<double>(
        (std::min)(kAxisRange, stroke * kAxisRange / kStrokeRange));
}

double zoneLevel(const std::array<std::uint8_t, 10>& strengths,
                 double position) noexcept {
    const double coordinate = position * kZonesPerPosition;
    const auto zone = static_cast<unsigned>(
        (std::clamp)(coordinate, 0.0, static_cast<double>(kZoneCount) - 0.001));
    const double level = strengths[zone];
    if (level == 0.0) return 0.0;
    const double offset = coordinate - (static_cast<double>(zone) + 0.5);
    if (offset < 0.0) {
        if (zone == 0) return level;
        const double neighbour = strengths[zone - 1];
        const double edge = neighbour > 0.0 ? (level + neighbour) / 2.0 : 0.0;
        return edge + (level - edge) * (offset + 0.5) / 0.5;
    }
    if (zone + 1 >= kZoneCount) return level;
    const double neighbour = strengths[zone + 1];
    const double edge = neighbour > 0.0 ? (level + neighbour) / 2.0 : 0.0;
    return level + (edge - level) * offset / 0.5;
}

double onset(double position, double start) noexcept {
    return (std::clamp)((position - start) / kApex6TriggerOnsetWidth, 0.0, 1.0);
}

unsigned burstDriveForForce(unsigned force) noexcept {
    if (force == 0) return 0;
    const double fraction = (std::min)(
        1.0, static_cast<double>(force) / kApex6TriggerBurstFullForce);
    const double drive = kApex6TriggerBurstMinimumDrive +
        (static_cast<double>(kNativeDrive) - kApex6TriggerBurstMinimumDrive) * fraction;
    return static_cast<unsigned>(std::lround(drive));
}

double impactLevel(const Apex6ImpactShape& shape, unsigned index) noexcept {
    const unsigned attack = shape.attackSamples;
    const unsigned hold = shape.holdSamples;
    const unsigned body = apex6ImpactBodySamples(shape);
    if (index < attack) {
        return (std::min)(1.0, static_cast<double>(index + 1) / static_cast<double>(attack));
    }
    if (index < attack + hold) return 1.0;
    if (index < body) {
        const double floorLevel = shape.tailSamples > 0 ? shape.tailLevel : 0.0;
        const double progress = static_cast<double>(index - attack - hold + 1) /
                                static_cast<double>(shape.releaseSamples);
        return 1.0 + (floorLevel - 1.0) * progress;
    }
    const unsigned tailIndex = index - body;
    if (tailIndex >= shape.tailSamples) return 0.0;
    const double end = std::exp(-kApex6TriggerTailDecayConstants);
    const double raw = std::exp(-kApex6TriggerTailDecayConstants *
                                static_cast<double>(tailIndex) /
                                static_cast<double>(shape.tailSamples));
    return shape.tailLevel * (raw - end) / (1.0 - end);
}

double segmentValue(const Apex6ImpactSegment& segment, unsigned index) noexcept {
    if (index < segment.startSample ||
        index >= static_cast<unsigned>(segment.startSample) + segment.lengthSamples) {
        return 0.0;
    }
    const double local = static_cast<double>(index - segment.startSample);
    double envelope = segment.riseSamples > 0.0
        ? 1.0 - std::exp(-local / segment.riseSamples) : 1.0;
    if (segment.decaySamples > 0.0 && local > segment.holdSamples) {
        envelope *= std::exp(-(local - segment.holdSamples) / segment.decaySamples);
    }
    return segment.amplitude * envelope *
           std::sin(2.0 * kPi * (segment.frequency * local / 1000.0 + segment.phase));
}

double shapeValue(const Apex6ImpactShape& shape, unsigned index) noexcept {
    if (shape.segmentCount != 0) {
        const auto count = (std::min)(static_cast<std::size_t>(shape.segmentCount),
                                      kApex6ImpactMaximumSegments);
        double value = 0.0;
        for (std::size_t segment = 0; segment < count; ++segment) {
            value += segmentValue(shape.segments[segment], index);
        }
        return (std::clamp)(value, -1.0, 1.0);
    }
    const unsigned body = apex6ImpactBodySamples(shape);
    const bool inTail = index >= body;
    const double frequency = inTail ? shape.tailFrequency : shape.frequency;
    const double local = static_cast<double>(inTail ? index - body : index);
    return std::sin(2.0 * kPi * frequency * local / 1000.0) * impactLevel(shape, index);
}

std::int8_t driveSample(double value) noexcept {
    return static_cast<std::int8_t>(std::clamp<std::int32_t>(
        static_cast<std::int32_t>(std::lround(value)), -kMaximumDrive, kMaximumDrive));
}

void startBurst(Apex6TriggerRenderState& state, const Apex6ImpactShape& shape,
                unsigned drive) noexcept {
    const unsigned total = apex6ImpactTotalSamples(shape);
    if (drive == 0 || total == 0) return;
    state.burstShape = shape;
    state.burstDrive = static_cast<std::uint8_t>((std::min)(drive, kMaximumBurstDrive));
    state.burstElapsed = 0;
    state.burstLength = static_cast<std::uint16_t>((std::min)(total, 65535U));
    ++state.burstsStarted;
}

bool crossedBreakPoint(Apex6TriggerRenderState& state, double position,
                       double rearm, double trigger) noexcept {
    if (position <= rearm) state.armed = true;
    if (!state.hasPreviousPosition || !state.armed ||
        static_cast<double>(state.previousPosition) >= trigger || position < trigger) {
        return false;
    }
    state.armed = false;
    return true;
}

struct Envelope {
    double amplitude = 0.0;
    double frequency = kApex6TriggerCarrierFrequency;
    bool continuous = false;
};

std::int8_t sample(double& phase, double frequency, double amplitude) noexcept {
    const auto value = static_cast<std::int32_t>(
        std::lround(std::sin(phase * 2.0 * kPi) * amplitude));
    phase += frequency / 1000.0;
    phase -= std::floor(phase);
    return static_cast<std::int8_t>(std::clamp<std::int32_t>(
        value, -kMaximumDrive, kMaximumDrive));
}

std::int8_t burstSample(Apex6TriggerRenderState& state) noexcept {
    const auto& shape = state.burstShape;
    const unsigned index = state.burstElapsed;
    const double value = shapeValue(shape, index) * state.burstDrive;
    if (index < apex6ImpactBodySamples(shape)) state.burstInFrame = true;
    ++state.burstElapsed;
    if (state.burstElapsed >= state.burstLength) {
        state.burstElapsed = 0;
        state.burstLength = 0;
    }
    return driveSample(value);
}

std::int8_t rhythmSample(const Apex6RhythmShape& shape, std::uint32_t elapsed) noexcept {
    if (shape.periodSamples == 0) return 0;
    const unsigned cycle = elapsed % shape.periodSamples;
    double value = 0.0;
    if (shape.modulated) {
        const double mix = 0.5 - 0.5 * std::cos(
            2.0 * kPi * static_cast<double>(cycle) / static_cast<double>(shape.periodSamples));
        const double drive = shape.levelA + (shape.levelB - shape.levelA) * mix;
        value = std::sin(2.0 * kPi * shape.pulse.frequency *
                         static_cast<double>(elapsed % 1000000U) / 1000.0) * drive;
    } else {
        const unsigned total = apex6ImpactTotalSamples(shape.pulse);
        if (cycle >= shape.firstPulseOffset && cycle - shape.firstPulseOffset < total) {
            value = shapeValue(shape.pulse, cycle - shape.firstPulseOffset) * shape.drive;
        }
        if (shape.secondPulseOffset != 0xFFFF && cycle >= shape.secondPulseOffset &&
            cycle - shape.secondPulseOffset < total) {
            value = shapeValue(shape.pulse, cycle - shape.secondPulseOffset) * shape.drive;
        }
    }
    return driveSample(value);
}

Apex6TriggerEffect rhythmFromPeriodic(const PeriodicEmulation& periodic) noexcept {
    Apex6TriggerEffect result{};
    result.type = Apex6TriggerType::Rhythm;
    auto& rhythm = result.rhythm;
    rhythm.activeFrom = positionFor(static_cast<std::uint8_t>(periodic.firstZone));
    rhythm.activeTo = periodic.lastZone + 1U >= kZoneCount
        ? static_cast<std::uint8_t>(kAxisRange)
        : static_cast<std::uint8_t>(
              positionFor(static_cast<std::uint8_t>(periodic.lastZone + 1U)) - 1U);
    if (periodic.kind == PeriodicEmulation::Kind::Galloping) {
        const unsigned period = (std::max)(
            1U, static_cast<unsigned>(std::lround(1000.0 / periodic.frequency)));
        const unsigned slot = period / kApex6TriggerGallopSlots;
        rhythm.periodSamples = static_cast<std::uint16_t>(period);
        rhythm.firstPulseOffset = static_cast<std::uint16_t>(periodic.firstFoot * slot);
        rhythm.secondPulseOffset = static_cast<std::uint16_t>(periodic.secondFoot * slot);
        rhythm.pulse.holdSamples = kApex6TriggerGallopHoldSamples;
        rhythm.pulse.releaseSamples = kApex6TriggerGallopReleaseSamples;
        rhythm.drive = static_cast<std::uint8_t>(kApex6TriggerGallopDrive);
        return result;
    }
    rhythm.modulated = true;
    rhythm.periodSamples = static_cast<std::uint16_t>(
        (std::min)(periodic.periodTenths * 100U, 65535U));
    rhythm.pulse.frequency = periodic.frequency;
    rhythm.levelA = periodic.amplitudeA * kApex6TriggerMachineDrivePerLevel;
    rhythm.levelB = periodic.amplitudeB * kApex6TriggerMachineDrivePerLevel;
    return result;
}

} // namespace

std::optional<Apex6TriggerEffect> decodeApex6TriggerEffect(
    TriggerSide side, const std::array<std::uint8_t, 11>& bytes,
    std::uint8_t fallbackMotor, Apex6TriggerDecodeError* error) noexcept {
    if (error) *error = Apex6TriggerDecodeError::None;
    Apex6TriggerEffect result{};
    if (bytes[0] == 0 || bytes[0] == 0x05) return result;

    if (bytes[0] == 0x21 || bytes[0] == 0x26 || bytes[0] == 0x25 || bytes[0] == 0x22) {
        const auto mask = static_cast<std::uint16_t>(bytes[1]) |
                          (static_cast<std::uint16_t>(bytes[2]) << 8U);
        if ((mask & ~std::uint16_t{0x03FF}) != 0) {
            if (error) *error = Apex6TriggerDecodeError::InvalidParameters;
            return std::nullopt;
        }
        if (mask == 0 || (bytes[0] == 0x26 && bytes[9] == 0)) return result;
        auto first = 10U;
        auto last = 0U;
        auto active = 0U;
        for (unsigned zone = 0; zone < 10; ++zone) {
            if ((mask & (std::uint16_t{1} << zone)) == 0) continue;
            first = (std::min)(first, zone);
            last = zone;
            ++active;
        }
        result.startZone = static_cast<std::uint8_t>(first);
        result.endZone = static_cast<std::uint8_t>(last);
        if (bytes[0] == 0x25 || bytes[0] == 0x22) {
            const bool bow = bytes[0] == 0x22;
            const auto forcePair = static_cast<unsigned>(bytes[3]) |
                                   (static_cast<unsigned>(bytes[4]) << 8U);
            if (active != 2 || first >= last ||
                (bow && (last > 8 || (forcePair & ~0x3FU) != 0))) {
                if (error) *error = Apex6TriggerDecodeError::InvalidParameters;
                return std::nullopt;
            }
            result.type = bow ? Apex6TriggerType::Bow : Apex6TriggerType::Weapon;
            result.strength = static_cast<std::uint8_t>((bytes[3] & 0x07U) + 1U);
            if (bow) result.snapStrength = static_cast<std::uint8_t>(
                ((forcePair >> 3U) & 0x07U) + 1U);
            return result;
        }

        const auto packed = static_cast<std::uint32_t>(bytes[3]) |
                            (static_cast<std::uint32_t>(bytes[4]) << 8U) |
                            (static_cast<std::uint32_t>(bytes[5]) << 16U) |
                            (static_cast<std::uint32_t>(bytes[6]) << 24U);
        for (unsigned zone = 0; zone < 10; ++zone) {
            if ((mask & (std::uint16_t{1} << zone)) != 0) {
                result.zoneStrengths[zone] = static_cast<std::uint8_t>(
                    ((packed >> (zone * 3U)) & 0x07U) + 1U);
            }
        }
        result.type = bytes[0] == 0x21
            ? Apex6TriggerType::Feedback : Apex6TriggerType::Vibration;
        result.frequency = bytes[0] == 0x26 ? bytes[9] : 0;
        return result;
    }

    if (bytes[0] == 0x23 || bytes[0] == 0x27) {
        if (const auto periodic = describePeriodicEmulation(bytes)) {
            return rhythmFromPeriodic(*periodic);
        }
    }

    const auto translated = translateAdaptiveTrigger(side, bytes, fallbackMotor);
    if (!translated) {
        if (error) *error = Apex6TriggerDecodeError::UnsupportedType;
        return std::nullopt;
    }
    if (translated->mode == TriggerMode::Normal) return result;
    result.type = Apex6TriggerType::Legacy;
    result.legacy = *translated;
    return result;
}

std::array<std::int8_t, 8> renderApex6TriggerEffect(
    const Apex6TriggerEffect& effect, std::uint8_t position,
    Apex6TriggerRenderState& state,
    std::optional<std::uint64_t> sampleOffset) noexcept {
    std::array<std::int8_t, 8> output{};
    Envelope envelope;
    state.burstInFrame = false;
    const double current = position;

    switch (effect.type) {
    case Apex6TriggerType::Off:
        state.burstElapsed = 0;
        state.burstLength = 0;
        break;
    case Apex6TriggerType::Feedback:
        envelope.amplitude = kApex6TriggerFeedbackDrivePerLevel *
                             zoneLevel(effect.zoneStrengths, current);
        envelope.continuous = true;
        break;
    case Apex6TriggerType::Vibration:
        envelope.continuous = true;
        if (effect.frequency == 0) break;
        envelope.amplitude = kApex6TriggerVibrationDrivePerLevel *
                             zoneLevel(effect.zoneStrengths, current);
        envelope.frequency = effect.frequency;
        break;
    case Apex6TriggerType::Weapon: {
        const double start = positionFor(effect.startZone);
        const double end = positionFor(effect.endZone);
        if (crossedBreakPoint(state, current, start + kApex6TriggerRearmMargin, end)) {
            startBurst(state, Apex6ImpactShape{}, burstDriveForForce(
                static_cast<unsigned>(effect.strength) * kApex6TriggerForcePerStrength));
        }
        break;
    }
    case Apex6TriggerType::Bow: {
        const double start = positionFor(effect.startZone);
        const double end = positionFor(effect.endZone);
        if (crossedBreakPoint(state, current, start + kApex6TriggerRearmMargin, end)) {
            startBurst(state, Apex6ImpactShape{}, burstDriveForForce(
                static_cast<unsigned>(effect.snapStrength) * kApex6TriggerForcePerStrength));
        }
        if (state.armed && current < end && end > start) {
            const double draw = (std::clamp)((current - start) / (end - start), 0.0, 1.0);
            envelope.amplitude = static_cast<double>(effect.strength) *
                                 kApex6TriggerBowDrivePerLevel *
                                 std::pow(draw, kApex6TriggerBowRampExponent);
        }
        break;
    }
    case Apex6TriggerType::Impact:
        if (crossedBreakPoint(state, current, effect.rearmPosition, effect.triggerPosition)) {
            startBurst(state, effect.impact, effect.impactDrive);
        }
        break;
    case Apex6TriggerType::Rhythm: {
        const double from = effect.rhythm.activeFrom;
        const double to = effect.rhythm.activeTo;
        const bool inside = current >= from && current <= to;
        const bool staying = current + kApex6TriggerRhythmReleaseMargin >= from &&
                             current <= to + kApex6TriggerRhythmReleaseMargin;
        if (!state.trainActive && inside) {
            state.trainActive = true;
            state.trainElapsed = 0;
        } else if (state.trainActive && !staying) {
            state.trainActive = false;
        }
        break;
    }
    case Apex6TriggerType::Legacy: {
        const auto& command = effect.legacy;
        const double start = axisForStroke(command.params[0]);
        switch (command.mode) {
        case TriggerMode::Race:
            envelope.amplitude = static_cast<double>(
                (std::min)(command.params[1], kNativeDrive)) * onset(current, start);
            break;
        case TriggerMode::SniperBreak: {
            const double end = (std::max)(
                axisForStroke(static_cast<unsigned>(command.params[0]) + command.params[1]),
                start + 1.0);
            if (crossedBreakPoint(state, current, start + kApex6TriggerRearmMargin, end)) {
                startBurst(state, Apex6ImpactShape{}, burstDriveForForce(command.params[2]));
            }
            break;
        }
        case TriggerMode::RecoilRattle:
        case TriggerMode::Vibration:
            envelope.amplitude = static_cast<double>(
                (std::min)(command.params[2], kNativeDrive)) * onset(current, start);
            envelope.frequency = command.params[3] == 0
                ? kDefaultLegacyFrequency : static_cast<double>(command.params[3]);
            break;
        case TriggerMode::Lock: {
            const double rearm = start > kApex6TriggerLockRearmMargin
                ? start - kApex6TriggerLockRearmMargin : 0.0;
            if (crossedBreakPoint(state, current, rearm, start)) {
                startBurst(state, Apex6ImpactShape{}, kNativeDrive);
            }
            break;
        }
        case TriggerMode::Normal:
            break;
        }
        break;
    }
    }

    state.previousPosition = position;
    state.hasPreviousPosition = true;
    if (envelope.continuous && sampleOffset) {
        state.phase = std::fmod(static_cast<double>(*sampleOffset % 1000U) *
                               envelope.frequency / 1000.0, 1.0);
    }
    const double target = std::isfinite(envelope.amplitude)
        ? (std::clamp)(envelope.amplitude, 0.0, static_cast<double>(kNativeDrive)) : 0.0;
    double rampStart = state.hasAmplitude ? state.amplitude : target;
    for (std::size_t index = 0; index < output.size(); ++index) {
        if (state.burstLength != 0) {
            output[index] = burstSample(state);
            rampStart = 0.0;
            continue;
        }
        if (effect.type == Apex6TriggerType::Rhythm && state.trainActive) {
            output[index] = rhythmSample(effect.rhythm, state.trainElapsed++);
            rampStart = 0.0;
            continue;
        }
        const double amplitude = rampStart + (target - rampStart) *
            static_cast<double>(index + 1) / static_cast<double>(output.size());
        output[index] = sample(state.phase, envelope.frequency, amplitude);
    }
    state.amplitude = state.burstInFrame ? 0.0 : target;
    state.hasAmplitude = true;
    return output;
}

namespace {

constexpr unsigned kTestMinimumLevel = 1;
constexpr unsigned kTestMaximumLevel = 4;
constexpr std::array<unsigned, 4> kTestStrengthByLevel{2, 4, 6, 8};
constexpr unsigned kTestSnapStrengthOffset = 3;
constexpr unsigned kTestMaximumStrength = 8;
constexpr unsigned kTestVibrationFrequency = 130;
constexpr unsigned kTestStrokeRange = 192;
constexpr unsigned kTestZoneCount = 10;
constexpr unsigned kTestForceStep = 32;
constexpr unsigned kTestResistanceDefaultZone = 1;
constexpr unsigned kTestWeaponDefaultStartZone = 2;
constexpr unsigned kTestWeaponDefaultEndZone = 6;
constexpr unsigned kTestWeaponMaximumStartZone = 7;
constexpr unsigned kTestWeaponMaximumEndZone = 8;
constexpr unsigned kTestWeaponSpan = 4;
constexpr unsigned kTestVibrationDefaultZone = 1;
constexpr unsigned kTestBowDefaultStartZone = 1;
constexpr unsigned kTestBowDefaultEndZone = 7;
constexpr unsigned kTestBowMaximumStartZone = 6;
constexpr unsigned kTestBowMaximumEndZone = 8;
constexpr unsigned kTestBowSpan = 6;
constexpr unsigned kTestSweepFullScale = 255;

void packZoneStrengths(std::array<std::uint8_t, 11>& bytes,
                       const std::array<unsigned, 10>& strengths) noexcept {
    std::uint32_t packed = 0;
    for (unsigned zone = 0; zone < kTestZoneCount; ++zone) {
        if (strengths[zone] == 0) continue;
        packed |= static_cast<std::uint32_t>(strengths[zone] - 1U) << (zone * 3U);
    }
    bytes[3] = static_cast<std::uint8_t>(packed);
    bytes[4] = static_cast<std::uint8_t>(packed >> 8U);
    bytes[5] = static_cast<std::uint8_t>(packed >> 16U);
    bytes[6] = static_cast<std::uint8_t>(packed >> 24U);
}

void setZoneMask(std::array<std::uint8_t, 11>& bytes, unsigned mask) noexcept {
    bytes[1] = static_cast<std::uint8_t>(mask);
    bytes[2] = static_cast<std::uint8_t>(mask >> 8U);
}

} // namespace

std::array<std::uint8_t, 11> makeApex6TestTriggerEffect(
    Apex6TestEffect effect, unsigned level,
    std::optional<std::uint8_t> start,
    std::optional<std::uint8_t> force) noexcept {
    std::array<std::uint8_t, 11> bytes{};
    if (effect == Apex6TestEffect::Off) return bytes;
    const auto index = (std::clamp)(level, kTestMinimumLevel, kTestMaximumLevel) - 1U;
    auto strength = kTestStrengthByLevel[index];
    if (force) {
        strength = (std::clamp)(
            (static_cast<unsigned>(*force) + kTestForceStep / 2U) / kTestForceStep,
            1U, kTestMaximumStrength);
    }
    const auto customZone = start
        ? (std::min)(kTestZoneCount - 1U,
                     static_cast<unsigned>(*start) * kTestZoneCount / kTestStrokeRange)
        : 0U;

    switch (effect) {
    case Apex6TestEffect::Off:
        break;
    case Apex6TestEffect::Resistance: {
        const auto first = start ? (std::min)(customZone, kTestZoneCount - 2U)
                                 : kTestResistanceDefaultZone;
        const auto last = kTestZoneCount - 1U;
        std::array<unsigned, 10> strengths{};
        unsigned mask = 0;
        for (unsigned zone = first; zone <= last; ++zone) {
            const auto span = last - first;
            strengths[zone] = 1U + ((strength - 1U) * (zone - first) + span / 2U) / span;
            mask |= 1U << zone;
        }
        bytes[0] = 0x21;
        setZoneMask(bytes, mask);
        packZoneStrengths(bytes, strengths);
        break;
    }
    case Apex6TestEffect::Weapon: {
        const auto first = start ? (std::clamp)(customZone, kTestWeaponDefaultStartZone,
                                                kTestWeaponMaximumStartZone)
                                 : kTestWeaponDefaultStartZone;
        const auto last = start ? (std::min)(kTestWeaponMaximumEndZone, first + kTestWeaponSpan)
                                : kTestWeaponDefaultEndZone;
        bytes[0] = 0x25;
        setZoneMask(bytes, (1U << first) | (1U << last));
        bytes[3] = static_cast<std::uint8_t>(strength - 1U);
        break;
    }
    case Apex6TestEffect::Vibration: {
        const auto first = start ? (std::min)(customZone, kTestZoneCount - 2U)
                                 : kTestVibrationDefaultZone;
        std::array<unsigned, 10> strengths{};
        unsigned mask = 0;
        for (unsigned zone = first; zone < kTestZoneCount; ++zone) {
            strengths[zone] = strength;
            mask |= 1U << zone;
        }
        bytes[0] = 0x26;
        setZoneMask(bytes, mask);
        packZoneStrengths(bytes, strengths);
        bytes[9] = static_cast<std::uint8_t>(kTestVibrationFrequency);
        break;
    }
    case Apex6TestEffect::Bow: {
        const auto first = start ? (std::min)(customZone, kTestBowMaximumStartZone)
                                 : kTestBowDefaultStartZone;
        const auto last = start ? (std::min)(kTestBowMaximumEndZone, first + kTestBowSpan)
                                : kTestBowDefaultEndZone;
        const auto snap = (std::min)(kTestMaximumStrength, strength + kTestSnapStrengthOffset);
        bytes[0] = 0x22;
        setZoneMask(bytes, (1U << first) | (1U << last));
        bytes[3] = static_cast<std::uint8_t>((strength - 1U) | ((snap - 1U) << 3U));
        break;
    }
    }
    return bytes;
}

std::uint8_t apex6TestSweepPosition(std::uint64_t elapsedMilliseconds) noexcept {
    const auto t = static_cast<std::uint32_t>(
        elapsedMilliseconds % kApex6TestSweepPeriodMilliseconds);
    if (t < kApex6TestSweepRiseMilliseconds) {
        return static_cast<std::uint8_t>(
            kTestSweepFullScale * t / kApex6TestSweepRiseMilliseconds);
    }
    if (t < kApex6TestSweepRiseMilliseconds + kApex6TestSweepHoldMilliseconds) {
        return static_cast<std::uint8_t>(kTestSweepFullScale);
    }
    const auto fall = t - kApex6TestSweepRiseMilliseconds - kApex6TestSweepHoldMilliseconds;
    if (fall < kApex6TestSweepFallMilliseconds) {
        return static_cast<std::uint8_t>(
            kTestSweepFullScale - kTestSweepFullScale * fall / kApex6TestSweepFallMilliseconds);
    }
    return 0;
}

namespace {

struct PresetRow {
    std::string_view name;
    bool train = false;
    std::uint8_t start = 0;
    double fullDrive = 0.0;
    std::uint16_t periodSamples = 0;
    Apex6ImpactShape shape{};
};

constexpr std::array<double, 4> kPresetLevelScale{0.5, 0.7, 0.85, 1.0};

constexpr double sceneDrive(unsigned gain) noexcept {
    return static_cast<double>(gain) * kApex6TriggerPresetDrivePerGain;
}

constexpr Apex6ImpactSegment segment(std::uint16_t start, std::uint16_t length,
                                     double frequency, double phase, double amplitude,
                                     double rise, double hold, double decay) noexcept {
    return Apex6ImpactSegment{.frequency = frequency, .phase = phase, .amplitude = amplitude,
                              .riseSamples = rise, .holdSamples = hold,
                              .decaySamples = decay, .startSample = start,
                              .lengthSamples = length};
}

constexpr Apex6ImpactShape waveform(std::initializer_list<Apex6ImpactSegment> segments) noexcept {
    Apex6ImpactShape shape{};
    shape.attackSamples = 0;
    shape.holdSamples = 0;
    shape.releaseSamples = 0;
    for (const auto& item : segments) shape.segments[shape.segmentCount++] = item;
    return shape;
}

constexpr Apex6ImpactShape kSniperShape{
    .frequency = 100.0, .tailFrequency = 80.0, .tailLevel = 0.30, .attackSamples = 1,
    .holdSamples = 60, .releaseSamples = 20, .tailSamples = 1000};

constexpr std::array<PresetRow, kApex6WeaponPresetCount> kPresetTable{{
    {.name = "machinegun", .train = true, .start = 80, .fullDrive = sceneDrive(29),
     .periodSamples = 94,
     .shape = waveform({segment(0, 48, 62.2, 0.07, 1.01, 2.4, 18.6, 13.0)})},
    {.name = "rifle", .train = true, .start = 80, .fullDrive = sceneDrive(32),
     .periodSamples = 84,
     .shape = waveform({segment(0, 49, 60.0, 0.07, 1.03, 2.3, 16.8, 9.7)})},
    {.name = "smg", .train = true, .start = 80, .fullDrive = sceneDrive(40),
     .periodSamples = 60,
     .shape = waveform({segment(0, 35, 56.4, 0.07, 1.02, 1.4, 13.6, 8.5)})},
    {.name = "lmg", .train = true, .start = 80, .fullDrive = sceneDrive(27),
     .periodSamples = 94,
     .shape = waveform({segment(0, 90, 66.0, 0.06, 0.94, 3.4, 37.8, 14.4)})},
    {.name = "pistol", .start = 50, .fullDrive = sceneDrive(28),
     .shape = waveform({segment(0, 53, 92.1, 0.95, 1.02, 0.1, 9.2, 11.7),
                        segment(37, 47, 159.8, 0.04, 0.46, 3.4, 31.3, 4.3),
                        segment(79, 99, 69.9, 0.55, 1.30, 4.9, 8.0, 21.1)})},
    {.name = "shotgun", .start = 50, .fullDrive = sceneDrive(28),
     .shape = waveform({segment(0, 54, 72.0, 0.99, 1.15, 3.5, 12.4, 12.6),
                        segment(47, 61, 128.2, 0.12, 0.56, 3.4, 39.0, 4.3),
                        segment(98, 152, 56.0, 0.50, 1.43, 7.7, 11.0, 25.3)})},
    {.name = "sniperrifle", .start = 178, .fullDrive = kNativeDrive, .shape = kSniperShape},
    {.name = "sword", .start = 80, .fullDrive = sceneDrive(36),
     .shape = waveform({segment(0, 53, 233.7, 0.97, 0.76, 4.1, 39.0, 51.6),
                        segment(51, 40, 222.2, 0.90, 0.41, 2.1, 17.0, 37.5),
                        segment(68, 39, 236.4, 0.71, 1.89, 16.3, 18.0, 6.6),
                        segment(131, 14, 231.6, 0.77, 0.19, 0.2, 2.0, 3.7)})},
    {.name = "blade", .start = 80, .fullDrive = sceneDrive(40),
     .shape = waveform({segment(0, 47, 217.7, 0.00, 0.53, 2.2, 46.0, 102.6),
                        segment(41, 19, 97.7, 0.91, 0.82, 0.3, 2.0, 6.5),
                        segment(46, 32, 219.6, 0.01, 0.47, 0.0, 14.5, 6.2),
                        segment(88, 18, 228.4, 0.01, 0.21, 0.6, 1.0, 4.0)})},
    {.name = "dagger", .start = 50, .fullDrive = sceneDrive(40),
     .shape = waveform({segment(1, 10, 20.0, 0.41, 2.26, 0.0, 0.8, 7.6),
                        segment(5, 35, 163.3, 0.18, 0.26, 0.0, 6.8, 0.5),
                        segment(36, 45, 238.2, 0.47, 0.30, 1.5, 34.5, 3.2)})},
    {.name = "axe", .start = 80, .fullDrive = sceneDrive(40),
     .shape = waveform({segment(0, 67, 69.9, 0.06, 1.00, 3.2, 49.0, 22.1),
                        segment(65, 205, 70.0, 0.61, 0.99, 8.2, 57.2, 26.2)})},
    {.name = "club", .start = 80, .fullDrive = sceneDrive(36),
     .shape = waveform({segment(0, 161, 68.0, 0.06, 0.96, 3.2, 86.6, 46.3)})},
}};

std::string normalisedPresetName(std::string_view name) {
    std::string result;
    for (const char character : name) {
        if (character >= 'A' && character <= 'Z') {
            result.push_back(static_cast<char>(character - 'A' + 'a'));
        } else if ((character >= 'a' && character <= 'z') ||
                   (character >= '0' && character <= '9')) {
            result.push_back(character);
        }
    }
    return result;
}

} // namespace

std::optional<Apex6WeaponPreset> apex6WeaponPresetFromName(
    std::string_view name) noexcept {
    std::string normalised;
    try {
        normalised = normalisedPresetName(name);
    } catch (...) {
        return std::nullopt;
    }
    if (normalised == "submachinegun") normalised = "smg";
    if (normalised == "lightmachinegun") normalised = "lmg";
    for (std::size_t index = 0; index < kPresetTable.size(); ++index) {
        if (kPresetTable[index].name == normalised) {
            return static_cast<Apex6WeaponPreset>(index);
        }
    }
    return std::nullopt;
}

std::string_view apex6WeaponPresetName(Apex6WeaponPreset preset) noexcept {
    return kPresetTable[static_cast<std::size_t>(preset)].name;
}

bool apex6WeaponPresetIsTrain(Apex6WeaponPreset preset) noexcept {
    return kPresetTable[static_cast<std::size_t>(preset)].train;
}

Apex6TriggerEffect makeApex6WeaponPreset(
    Apex6WeaponPreset preset, unsigned level,
    std::optional<std::uint8_t> startPosition) noexcept {
    const auto& row = kPresetTable[static_cast<std::size_t>(preset)];
    const double scale = kPresetLevelScale[(std::clamp)(level, 1U, 4U) - 1U];
    const unsigned start = startPosition ? *startPosition : row.start;
    const auto drive = static_cast<std::uint8_t>((std::min)(
        static_cast<long>(kMaximumBurstDrive), std::lround(row.fullDrive * scale)));
    const auto& shape = row.shape;

    Apex6TriggerEffect effect{};
    if (row.train) {
        effect.type = Apex6TriggerType::Rhythm;
        effect.rhythm.pulse = shape;
        effect.rhythm.drive = drive;
        effect.rhythm.periodSamples = static_cast<std::uint16_t>((std::max)(
            static_cast<unsigned>(row.periodSamples), apex6ImpactTotalSamples(shape)));
        effect.rhythm.activeFrom = static_cast<std::uint8_t>(start);
        effect.rhythm.activeTo = static_cast<std::uint8_t>(kAxisRange);
        return effect;
    }
    effect.type = Apex6TriggerType::Impact;
    effect.impact = shape;
    effect.impactDrive = drive;
    effect.triggerPosition = static_cast<std::uint8_t>(start);
    effect.rearmPosition = static_cast<std::uint8_t>(
        start > kApex6TriggerPresetRearmMargin ? start - kApex6TriggerPresetRearmMargin : 0U);
    return effect;
}

} // namespace asb::dualsense
