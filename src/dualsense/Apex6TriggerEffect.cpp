#include "dualsense/Apex6TriggerEffect.h"

#include "dualsense/AdaptiveTriggerTranslation.h"

#include <algorithm>
#include <cmath>

namespace asb::dualsense {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint8_t kMaximumDrive = 96;

std::uint8_t zoneFor(std::uint8_t position) noexcept {
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(position) * 10U) / 256U);
}

std::uint8_t positionFor(std::uint8_t zone) noexcept {
    return static_cast<std::uint8_t>((static_cast<unsigned>(zone) * 256U + 9U) / 10U);
}

std::int8_t sample(double& phase, double frequency,
                   std::uint8_t amplitude) noexcept {
    const auto value = static_cast<std::int32_t>(
        std::lround(std::sin(phase * 2.0 * kPi) * amplitude));
    phase += frequency / 1000.0;
    phase -= std::floor(phase);
    return static_cast<std::int8_t>(std::clamp<std::int32_t>(
        value, -kMaximumDrive, kMaximumDrive));
}

} // namespace

std::optional<Apex6TriggerEffect> decodeApex6TriggerEffect(
    TriggerSide side, const std::array<std::uint8_t, 11>& bytes,
    std::uint8_t fallbackMotor) noexcept {
    Apex6TriggerEffect result{};
    if (bytes[0] == 0 || bytes[0] == 0x05) return result;

    if (bytes[0] == 0x21 || bytes[0] == 0x26 || bytes[0] == 0x25) {
        const auto mask = static_cast<std::uint16_t>(bytes[1]) |
                          (static_cast<std::uint16_t>(bytes[2]) << 8U);
        if ((mask & ~std::uint16_t{0x03FF}) != 0 || mask == 0) {
            return std::nullopt;
        }
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
        if (bytes[0] == 0x25) {
            if (active != 2 || first >= last) return std::nullopt;
            result.type = Apex6TriggerType::Weapon;
            result.strength = static_cast<std::uint8_t>((bytes[3] & 0x07U) + 1U);
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
        result.frequency = bytes[0] == 0x26 ? bytes[10] : 0;
        return result;
    }

    const auto translated = translateAdaptiveTrigger(side, bytes, fallbackMotor);
    if (!translated) return std::nullopt;
    if (translated->mode == TriggerMode::Normal) return result;
    result.type = Apex6TriggerType::Legacy;
    result.legacy = *translated;
    return result;
}

std::array<std::int8_t, 8> renderApex6TriggerEffect(
    const Apex6TriggerEffect& effect, std::uint8_t position,
    Apex6TriggerRenderState& state) noexcept {
    std::array<std::int8_t, 8> output{};
    std::uint8_t amplitude = 0;
    double frequency = 85.0;

    switch (effect.type) {
    case Apex6TriggerType::Off:
        break;
    case Apex6TriggerType::Feedback:
        // A VCM cannot exert DualSense-style static resistance. Preserve the
        // requested position and per-zone strength with a bounded texture.
        amplitude = static_cast<std::uint8_t>(
            effect.zoneStrengths[zoneFor(position)] * 8U);
        break;
    case Apex6TriggerType::Vibration:
        amplitude = static_cast<std::uint8_t>(
            effect.zoneStrengths[zoneFor(position)] * 10U);
        frequency = effect.frequency;
        if (effect.frequency == 0) amplitude = 0;
        break;
    case Apex6TriggerType::Weapon: {
        const auto start = positionFor(effect.startZone);
        const auto end = positionFor(effect.endZone);
        if (position < start) {
            state.weaponArmed = true;
            state.pulseSamplesRemaining = 0;
        } else if (state.hasPreviousPosition && state.weaponArmed &&
                   state.previousPosition < end && position >= end) {
            state.pulseSamplesRemaining = 16; // one brief break, not a held buzz
            state.weaponArmed = false;
        }
        if (state.pulseSamplesRemaining > 0) {
            amplitude = static_cast<std::uint8_t>(effect.strength * 12U);
            frequency = 110.0;
        } else if (position >= start && position < end) {
            amplitude = static_cast<std::uint8_t>(effect.strength * 5U);
        }
        break;
    }
    case Apex6TriggerType::Legacy: {
        const auto& command = effect.legacy;
        if (command.mode == TriggerMode::Normal ||
            position < command.params[0]) break;
        switch (command.mode) {
        case TriggerMode::Race:
            amplitude = command.params[1];
            break;
        case TriggerMode::SniperBreak:
            amplitude = (std::max)(command.params[1], command.params[2]);
            frequency = 110.0;
            break;
        case TriggerMode::RecoilRattle:
        case TriggerMode::Vibration:
            amplitude = command.params[2];
            frequency = command.params[3] == 0 ? 85.0 : command.params[3];
            break;
        case TriggerMode::Lock:
            amplitude = kMaximumDrive;
            break;
        case TriggerMode::Normal:
            break;
        }
        break;
    }
    }

    state.previousPosition = position;
    state.hasPreviousPosition = true;
    if (amplitude == 0) return output;
    amplitude = (std::min)(amplitude, kMaximumDrive);
    for (auto& value : output) {
        value = sample(state.phase, frequency, amplitude);
        if (effect.type == Apex6TriggerType::Weapon &&
            state.pulseSamplesRemaining > 0) {
            --state.pulseSamplesRemaining;
        }
    }
    return output;
}

} // namespace asb::dualsense
