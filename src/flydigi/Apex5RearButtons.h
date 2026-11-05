#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// APEX 5 rear/side buttons, decoded from the vendor HID input interface
// (UsagePage 0xFFA0, MI_02, 32-byte input report). Observed on hardware:
// each press sets exactly one bit in byte 14 and releasing clears it.
namespace asb::flydigi::apex5 {

inline constexpr std::size_t kRearButtonByteOffset = 14;

enum class RearButton : std::uint8_t {
    M1 = 0x04,
    M2 = 0x08,
    M3 = 0x10,
    M4 = 0x20,
    LM = 0x40,
    RM = 0x80,
};

[[nodiscard]] constexpr std::uint8_t rearButtonMask(RearButton button) noexcept {
    return static_cast<std::uint8_t>(button);
}

// Returns the raw button bits, or 0 if the report is too short to contain them.
[[nodiscard]] inline std::uint8_t rearButtonBits(std::span<const std::uint8_t> report) noexcept {
    if (report.size() <= kRearButtonByteOffset) {
        return 0;
    }
    return report[kRearButtonByteOffset];
}

[[nodiscard]] inline bool isRearButtonPressed(std::span<const std::uint8_t> report,
                                              RearButton button) noexcept {
    return (rearButtonBits(report) & rearButtonMask(button)) != 0;
}

} // namespace asb::flydigi::apex5