#pragma once

#include "core/TriggerEffect.h"

#include <array>
#include <cstdint>
#include <optional>

namespace asb::dualsense {

struct BowEmulation {
    ForceTriggerCommand draw;
    ForceTriggerCommand rupture;
    std::uint8_t ruptureAxis = 0;
    std::uint8_t rearmAxis = 0;
};

std::optional<BowEmulation> describeBowEmulation(
    TriggerSide side, const std::array<std::uint8_t, 11>& effect);

struct ZoneEmulation {
    std::array<std::uint8_t, 10> strength{};
    std::uint8_t frequency = 0;
};

inline constexpr std::array<std::uint8_t, 8> kPerceptualForceTable{1, 16, 32, 64, 96, 128, 160, 192};
inline constexpr std::array<std::uint8_t, 8> kPerceptualVibrationTable{1, 15, 25, 35, 45, 55, 65, 80};
inline constexpr std::uint8_t kMinimumEmulatedVibrationFrequency = 25;

struct ZoneUnits {
    std::uint8_t force = 8;
    std::uint8_t vibration = 10;
    std::array<std::uint8_t, 8> forceTable{};
    std::array<std::uint8_t, 8> vibrationTable{};
    double gallopingBeatSlots = 0.6;
    double gallopingMinimumBeatSeconds = 0.15;
    double gallopingBottomMinimumBeatSeconds = 0.04;
    std::uint8_t machineAmplitudeFloor = 40;
    std::uint8_t machineBottomAmplitudeFloor = 0;
    std::uint8_t gallopingAmplitude = 60;
    std::uint8_t gallopingCarrier = 30;
};

inline constexpr unsigned kZoneHysteresis = 6;
inline constexpr unsigned kVibrationZoneHysteresis = 30;

class ZoneTracker {
public:
    explicit ZoneTracker(std::uint8_t axis = 0) noexcept;
    unsigned update(std::uint8_t axis, unsigned hysteresis = kZoneHysteresis) noexcept;
    [[nodiscard]] unsigned zone() const noexcept { return zone_; }

private:
    unsigned zone_ = 0;
};

struct PeriodicEmulation {
    enum class Kind : std::uint8_t { Galloping, Machine };
    Kind kind = Kind::Galloping;
    unsigned firstZone = 0;
    unsigned lastZone = 0;
    unsigned firstFoot = 0;
    unsigned secondFoot = 0;
    unsigned amplitudeA = 0;
    unsigned amplitudeB = 0;
    unsigned periodTenths = 0;
    std::uint8_t frequency = 0;
};

inline constexpr unsigned kMaximumGallopingFrequency = 8;
inline constexpr unsigned kBottomTriggerPosition = 235;
inline constexpr unsigned kMinimumMachinePeriodTenths = 4;

std::optional<PeriodicEmulation> describePeriodicEmulation(
    const std::array<std::uint8_t, 11>& effect);

ZoneUnits productionZoneUnits() noexcept;

ZoneUnits periodicUnitsForPosition(ZoneUnits units, unsigned position) noexcept;

ForceTriggerCommand renderPeriodicCommand(
    TriggerSide side, const PeriodicEmulation& emulation, double seconds, bool inRange,
    ZoneUnits units = {});

std::optional<ZoneEmulation> describeZoneEmulation(
    const std::array<std::uint8_t, 11>& effect);

ForceTriggerCommand renderZoneCommand(
    TriggerSide side, const ZoneEmulation& emulation, unsigned zone, ZoneUnits units = {});

std::optional<ForceTriggerCommand> translateAdaptiveTrigger(
    TriggerSide side,
    const std::array<std::uint8_t, 11>& effect,
    std::uint8_t);

} // namespace asb::dualsense
