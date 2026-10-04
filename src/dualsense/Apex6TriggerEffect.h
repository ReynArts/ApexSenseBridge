#pragma once

#include "core/TriggerEffect.h"

#include <array>
#include <cstdint>
#include <optional>

namespace asb::dualsense {

enum class Apex6TriggerType : std::uint8_t {
    Off,
    Feedback,
    Weapon,
    Vibration,
    Legacy,
    Bow,
};

enum class Apex6TriggerDecodeError : std::uint8_t {
    None,
    UnsupportedType,
    InvalidParameters,
};

struct Apex6TriggerEffect {
    Apex6TriggerType type = Apex6TriggerType::Off;
    std::array<std::uint8_t, 10> zoneStrengths{};
    std::uint8_t startZone = 0;
    std::uint8_t endZone = 0;
    std::uint8_t strength = 0;
    std::uint8_t frequency = 0;
    std::uint8_t snapStrength = 0;
    ForceTriggerCommand legacy{};

    bool operator==(const Apex6TriggerEffect&) const = default;
};

struct Apex6TriggerRenderState {
    double phase = 0.0;
    std::uint8_t previousPosition = 0;
    std::uint8_t pulseSamplesRemaining = 0;
    bool hasPreviousPosition = false;
    bool weaponArmed = true;

    void reset() noexcept { *this = {}; }
};

// Native DualSense zone effects store ten three-bit strengths in bytes 3..6.
// An empty optional means an unsupported or malformed command; Off is explicit.
[[nodiscard]] std::optional<Apex6TriggerEffect> decodeApex6TriggerEffect(
    TriggerSide side, const std::array<std::uint8_t, 11>& effect,
    std::uint8_t fallbackMotor,
    Apex6TriggerDecodeError* error = nullptr) noexcept;

[[nodiscard]] std::array<std::int8_t, 8> renderApex6TriggerEffect(
    const Apex6TriggerEffect& effect, std::uint8_t position,
    Apex6TriggerRenderState& state,
    std::optional<std::uint64_t> sampleOffset = std::nullopt) noexcept;

} // namespace asb::dualsense
