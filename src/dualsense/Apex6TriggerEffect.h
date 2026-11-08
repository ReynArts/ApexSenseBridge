#pragma once

#include "core/TriggerEffect.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace asb::dualsense {

enum class Apex6TriggerType : std::uint8_t {
    Off,
    Feedback,
    Weapon,
    Vibration,
    Legacy,
    Bow,
    Impact,
    Rhythm,
};

enum class Apex6TriggerDecodeError : std::uint8_t {
    None,
    UnsupportedType,
    InvalidParameters,
};

inline constexpr std::uint8_t kApex6TriggerMaximumDrive = 127;
inline constexpr std::uint8_t kApex6TriggerNativeMaximumDrive = 96;
inline constexpr std::uint8_t kApex6TriggerSniperDrive = 96;
inline constexpr double kApex6TriggerPresetDrivePerGain = 4.0;
inline constexpr std::size_t kApex6ImpactMaximumSegments = 4;
inline constexpr double kApex6TriggerCarrierFrequency = 85.0;
inline constexpr double kApex6TriggerBurstFrequency = 95.0;
inline constexpr unsigned kApex6TriggerPulseTrainMaximumFrequency = 30;
inline constexpr unsigned kApex6TriggerDetentBaseDrive = 56;
inline constexpr unsigned kApex6TriggerDetentDrivePerStrength = 8;
inline constexpr unsigned kApex6TriggerDetentMaximumDrive = 100;
inline constexpr unsigned kApex6TriggerDetentHoldSamples = 14;
inline constexpr unsigned kApex6TriggerDetentReleaseSamples = 10;
inline constexpr double kApex6TriggerDetentRearmMargin = 20.0;
inline constexpr double kApex6TriggerDetentMinimumPosition = 10.0;
inline constexpr double kApex6TriggerContinuousReleasePosition = 8.0;
inline constexpr unsigned kApex6TriggerDetentFeedbackWindowMilliseconds = 500;
inline constexpr unsigned kApex6TriggerPulseTrainBaseDrive = 32;
inline constexpr double kApex6TriggerPulseTrainDriveStep = 19.0;
inline constexpr unsigned kApex6TriggerBurstAttackSamples = 1;
inline constexpr unsigned kApex6TriggerBurstHoldSamples = 40;
inline constexpr unsigned kApex6TriggerBurstReleaseSamples = 10;
inline constexpr unsigned kApex6TriggerBurstMinimumDrive = 70;
inline constexpr unsigned kApex6TriggerBurstMaximumDrive = 127;
inline constexpr unsigned kApex6TriggerBurstFullForce = 48;
inline constexpr unsigned kApex6TriggerForcePerStrength = 8;
inline constexpr double kApex6TriggerTailDecayConstants = 4.0;
inline constexpr double kApex6TriggerFeedbackDrivePerLevel = 8.0;
inline constexpr double kApex6TriggerVibrationDrivePerLevel = 10.0;
inline constexpr double kApex6TriggerLevelExponent = 1.0;
inline constexpr double kApex6TriggerBowDrivePerLevel = 6.0;
inline constexpr double kApex6TriggerBowRampExponent = 1.0;
inline constexpr double kApex6TriggerMachineDrivePerLevel = 12.0;
inline constexpr double kApex6TriggerGallopDrive = 64.0;
inline constexpr unsigned kApex6TriggerGallopHoldSamples = 20;
inline constexpr unsigned kApex6TriggerGallopReleaseSamples = 8;
inline constexpr unsigned kApex6TriggerGallopSlots = 8;
inline constexpr double kApex6TriggerOnsetWidth = 13.0;
inline constexpr unsigned kApex6TriggerRearmMargin = 4;
inline constexpr unsigned kApex6TriggerLockRearmMargin = 8;
inline constexpr unsigned kApex6TriggerRhythmReleaseMargin = 6;
inline constexpr unsigned kApex6TriggerPresetWindowWidth = 10;
inline constexpr unsigned kApex6TriggerPresetRearmMargin = 6;
inline constexpr unsigned kApex6TriggerPresetTrainStartPosition = 80;


struct Apex6ImpactSegment {
    double frequency = 0.0;
    double phase = 0.0;
    double amplitude = 0.0;
    double riseSamples = 0.0;
    double holdSamples = 0.0;
    double decaySamples = 0.0;
    std::uint16_t startSample = 0;
    std::uint16_t lengthSamples = 0;

    bool operator==(const Apex6ImpactSegment&) const = default;
};

struct Apex6ImpactShape {
    double frequency = kApex6TriggerBurstFrequency;
    double tailFrequency = kApex6TriggerCarrierFrequency;
    double tailLevel = 0.0;
    std::uint16_t attackSamples = kApex6TriggerBurstAttackSamples;
    std::uint16_t holdSamples = kApex6TriggerBurstHoldSamples;
    std::uint16_t releaseSamples = kApex6TriggerBurstReleaseSamples;
    std::uint16_t tailSamples = 0;
    std::array<Apex6ImpactSegment, kApex6ImpactMaximumSegments> segments{};
    std::uint8_t segmentCount = 0;

    bool operator==(const Apex6ImpactShape&) const = default;
};

[[nodiscard]] constexpr unsigned apex6ImpactSegmentSamples(
    const Apex6ImpactShape& shape) noexcept {
    unsigned end = 0;
    const auto count = shape.segmentCount < kApex6ImpactMaximumSegments
        ? static_cast<std::size_t>(shape.segmentCount) : kApex6ImpactMaximumSegments;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& segment = shape.segments[index];
        const unsigned segmentEnd =
            static_cast<unsigned>(segment.startSample) + segment.lengthSamples;
        if (segmentEnd > end) end = segmentEnd;
    }
    return end;
}

[[nodiscard]] constexpr unsigned apex6ImpactBodySamples(
    const Apex6ImpactShape& shape) noexcept {
    if (shape.segmentCount != 0) return apex6ImpactSegmentSamples(shape);
    return static_cast<unsigned>(shape.attackSamples) + shape.holdSamples +
           shape.releaseSamples;
}

[[nodiscard]] constexpr unsigned apex6ImpactTotalSamples(
    const Apex6ImpactShape& shape) noexcept {
    return apex6ImpactBodySamples(shape) + shape.tailSamples;
}

[[nodiscard]] constexpr unsigned apex6ImpactFrames(
    const Apex6ImpactShape& shape) noexcept {
    return (apex6ImpactTotalSamples(shape) + 7U) / 8U;
}

struct Apex6RhythmShape {
    Apex6ImpactShape pulse{};
    double levelA = 0.0;
    double levelB = 0.0;
    std::uint16_t periodSamples = 0;
    std::uint16_t firstPulseOffset = 0;
    std::uint16_t secondPulseOffset = 0xFFFF;
    std::uint8_t drive = 0;
    std::uint8_t activeFrom = 0;
    std::uint8_t activeTo = 255;
    bool modulated = false;

    bool operator==(const Apex6RhythmShape&) const = default;
};

struct Apex6TriggerEffect {
    Apex6TriggerType type = Apex6TriggerType::Off;
    std::array<std::uint8_t, 10> zoneStrengths{};
    std::uint8_t startZone = 0;
    std::uint8_t endZone = 0;
    std::uint8_t strength = 0;
    std::uint8_t frequency = 0;
    std::uint8_t snapStrength = 0;
    std::uint8_t triggerPosition = 0;
    std::uint8_t rearmPosition = 0;
    std::uint8_t impactDrive = 0;
    Apex6ImpactShape impact{};
    Apex6RhythmShape rhythm{};
    ForceTriggerCommand legacy{};
    bool detent = false;

    bool operator==(const Apex6TriggerEffect&) const = default;
};

struct Apex6TriggerRenderState {
    double phase = 0.0;
    double amplitude = 0.0;
    Apex6ImpactShape burstShape{};
    std::uint64_t burstsStarted = 0;
    std::uint32_t trainElapsed = 0;
    std::uint16_t burstElapsed = 0;
    std::uint16_t burstLength = 0;
    std::uint8_t previousPosition = 0;
    std::uint8_t burstDrive = 0;
    bool hasPreviousPosition = false;
    bool hasAmplitude = false;
    bool armed = true;
    bool burstInFrame = false;
    bool trainActive = false;
    bool detentPending = false;

    void reset() noexcept { *this = {}; }

    void retarget() noexcept {
        const Apex6TriggerRenderState kept = *this;
        *this = {};
        phase = kept.phase;
        amplitude = kept.amplitude;
        hasAmplitude = kept.hasAmplitude && kept.amplitude > 0.0;
        burstShape = kept.burstShape;
        burstElapsed = kept.burstElapsed;
        burstLength = kept.burstLength;
        burstDrive = kept.burstDrive;
        burstsStarted = kept.burstsStarted;
        previousPosition = kept.previousPosition;
        hasPreviousPosition = kept.hasPreviousPosition;
        armed = kept.armed;
    }
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

enum class Apex6TestEffect : std::uint8_t {
    Off,
    Resistance,
    Weapon,
    Vibration,
    Bow,
};

inline constexpr std::uint32_t kApex6TestSweepRiseMilliseconds = 1000;
inline constexpr std::uint32_t kApex6TestSweepHoldMilliseconds = 250;
inline constexpr std::uint32_t kApex6TestSweepFallMilliseconds = 500;
inline constexpr std::uint32_t kApex6TestSweepRestMilliseconds = 250;
inline constexpr std::uint32_t kApex6TestSweepPeriodMilliseconds =
    kApex6TestSweepRiseMilliseconds + kApex6TestSweepHoldMilliseconds +
    kApex6TestSweepFallMilliseconds + kApex6TestSweepRestMilliseconds;

[[nodiscard]] std::array<std::uint8_t, 11> makeApex6TestTriggerEffect(
    Apex6TestEffect effect, unsigned level,
    std::optional<std::uint8_t> start = std::nullopt,
    std::optional<std::uint8_t> force = std::nullopt) noexcept;

[[nodiscard]] std::uint8_t apex6TestSweepPosition(
    std::uint64_t elapsedMilliseconds) noexcept;

enum class Apex6WeaponPreset : std::uint8_t {
    MachineGun,
    Rifle,
    SubmachineGun,
    LightMachineGun,
    Pistol,
    Shotgun,
    SniperRifle,
    Sword,
    Blade,
    Dagger,
    Axe,
    Club,
};

inline constexpr std::size_t kApex6WeaponPresetCount = 12;

[[nodiscard]] std::optional<Apex6WeaponPreset> apex6WeaponPresetFromName(
    std::string_view name) noexcept;

[[nodiscard]] std::string_view apex6WeaponPresetName(
    Apex6WeaponPreset preset) noexcept;

[[nodiscard]] bool apex6WeaponPresetIsTrain(Apex6WeaponPreset preset) noexcept;

[[nodiscard]] Apex6TriggerEffect makeApex6WeaponPreset(
    Apex6WeaponPreset preset, unsigned level,
    std::optional<std::uint8_t> startPosition = std::nullopt) noexcept;

} // namespace asb::dualsense
