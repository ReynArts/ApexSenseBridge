#pragma once

#include <chrono>
#include <cstdint>

namespace asb::flydigi {

enum class Apex4WriteKind : std::uint8_t {
    None,
    Trigger,
    Rumble,
};

constexpr auto kApex4TriggerWriteSpacing = std::chrono::milliseconds(25);
constexpr auto kApex4RumbleWriteSpacing = std::chrono::milliseconds(10);
constexpr auto kApex4MinimumWriteSpacing = std::chrono::milliseconds(10);
constexpr auto kApex4MaximumWriteSpacing = std::chrono::milliseconds(50);
constexpr const char* kApex4RumbleSpacingVariable = "ASB_APEX4_RUMBLE_MS";

struct Apex4RumbleSpacing {
    std::chrono::milliseconds spacing = kApex4RumbleWriteSpacing;
    bool overridden = false;
};

[[nodiscard]] Apex4RumbleSpacing parseApex4RumbleSpacing(const char* text) noexcept;
[[nodiscard]] Apex4RumbleSpacing configuredApex4RumbleSpacing() noexcept;

[[nodiscard]] std::chrono::milliseconds apex4WriteSpacing(
    Apex4WriteKind previous,
    Apex4WriteKind next,
    std::chrono::milliseconds rumbleSpacing) noexcept;

class Apex4WritePacer {
public:
    using Clock = std::chrono::steady_clock;

    Apex4WritePacer() noexcept = default;
    explicit Apex4WritePacer(std::chrono::milliseconds rumbleSpacing) noexcept;

    [[nodiscard]] std::chrono::milliseconds rumbleSpacing() const noexcept { return rumbleSpacing_; }
    [[nodiscard]] Apex4WriteKind lastKind() const noexcept { return lastKind_; }
    [[nodiscard]] Clock::time_point lastWriteAt() const noexcept { return lastWriteAt_; }
    [[nodiscard]] Clock::time_point earliest(Apex4WriteKind next) const noexcept;
    void record(Apex4WriteKind kind, Clock::time_point at) noexcept;

private:
    std::chrono::milliseconds rumbleSpacing_ = kApex4RumbleWriteSpacing;
    Apex4WriteKind lastKind_ = Apex4WriteKind::None;
    Clock::time_point lastWriteAt_{};
};

} // namespace asb::flydigi
