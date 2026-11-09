#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace asb::platform {

inline constexpr std::uint32_t kSessionTuningMagic = 0x54425341;
inline constexpr std::uint32_t kSessionTuningVersion = 1;
inline constexpr std::uint32_t kSessionTuningKeep = 0xFFFFFFFFU;
inline constexpr std::uint32_t kSessionTuningNoSlot = 0xFFU;

struct SessionTuningRequest {
    std::uint32_t sequence = 0;
    std::uint32_t triggerStrengthPercent = kSessionTuningKeep;
    std::uint32_t vibrationStrengthPercent = kSessionTuningKeep;
    std::uint32_t gripMute = kSessionTuningKeep;
    std::uint32_t triggerEffectsOff = kSessionTuningKeep;
    std::uint32_t touchpadProfile = kSessionTuningKeep;
    std::uint32_t apexProfileSlot = kSessionTuningKeep;
    std::uint32_t reserved = 0;
};
static_assert(sizeof(SessionTuningRequest) == 32);

struct SessionTuningApplied {
    std::uint32_t sequence = 0;
    std::uint32_t requestSequence = 0;
    std::uint32_t triggerStrengthPercent = 0;
    std::uint32_t vibrationStrengthPercent = 0;
    std::uint32_t gripMute = 0;
    std::uint32_t triggerEffectsOff = 0;
    std::uint32_t touchpadProfile = 0;
    std::uint32_t apexProfileSlot = kSessionTuningNoSlot;
    std::uint32_t sampleRateHz = 0;
    std::uint32_t controllerModel = 0;
    std::uint32_t reserved[2] = {};
};
static_assert(sizeof(SessionTuningApplied) == 48);

struct SessionTuningBlock {
    std::uint32_t magic = kSessionTuningMagic;
    std::uint32_t version = kSessionTuningVersion;
    SessionTuningRequest request{};
    SessionTuningApplied applied{};
    std::uint32_t reserved[10] = {};
};
static_assert(sizeof(SessionTuningBlock) == 128);

inline constexpr std::size_t kSessionTuningBlockSize = sizeof(SessionTuningBlock);

struct SessionTuningLimits {
    std::uint32_t maxVibrationPercent = 100;
    std::uint32_t maxTouchpadProfile = 0;
    std::uint32_t maxApexSlot = 3;
};

struct SessionTuningChange {
    std::optional<std::uint32_t> triggerStrengthPercent;
    std::optional<std::uint32_t> vibrationStrengthPercent;
    std::optional<bool> gripMute;
    std::optional<bool> triggerEffectsOff;
    std::optional<std::uint32_t> touchpadProfile;
    std::optional<std::uint32_t> apexProfileSlot;

    [[nodiscard]] bool any() const noexcept {
        return triggerStrengthPercent || vibrationStrengthPercent || gripMute ||
               triggerEffectsOff || touchpadProfile || apexProfileSlot;
    }
};

[[nodiscard]] std::string sessionTuningMappingName(std::string_view token);

[[nodiscard]] SessionTuningChange interpretSessionTuningRequest(
    const SessionTuningRequest& request,
    const SessionTuningLimits& limits) noexcept;

class SessionTuning {
public:
    virtual ~SessionTuning() = default;

    [[nodiscard]] virtual bool poll(SessionTuningRequest& request) noexcept = 0;
    virtual void publishApplied(const SessionTuningApplied& applied) noexcept = 0;
};

[[nodiscard]] std::unique_ptr<SessionTuning> createSessionTuning(
    std::string_view token, std::string& error);

} // namespace asb::platform
