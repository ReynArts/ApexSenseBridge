#pragma once

#include "dualsense/DualSenseInput.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace asb::platform::rear {

// Order matches the APEX 5 rear-button bits: M1, M2, M3, M4, LM, RM.
inline constexpr std::size_t kRearButtonCount = 6;

inline constexpr std::array<std::string_view, kRearButtonCount> kRearButtonNames{
    "M1", "M2", "M3", "M4", "LM", "RM"};

struct ActionEntry {
    std::string_view name;
    std::uint16_t bit;
};

// "None" maps to bit 0 (no output). The 15 non-None entries are the
// phase-A action set confirmed for the Tray UI.
inline constexpr std::array<ActionEntry, 16> kActionTable{{
    {"None", 0},
    {"Cross", dualsense::button::kCross},
    {"Circle", dualsense::button::kCircle},
    {"Square", dualsense::button::kSquare},
    {"Triangle", dualsense::button::kTriangle},
    {"L1", dualsense::button::kL1},
    {"R1", dualsense::button::kR1},
    {"L2", dualsense::button::kL2},
    {"R2", dualsense::button::kR2},
    {"L3", dualsense::button::kL3},
    {"R3", dualsense::button::kR3},
    {"Create", dualsense::button::kCreate},
    {"Options", dualsense::button::kOptions},
    {"PS", dualsense::button::kPs},
    {"Touchpad", dualsense::button::kTouchpadClick},
    {"Mute", dualsense::button::kMute},
}};

[[nodiscard]] inline bool lookupAction(std::string_view name, std::uint16_t& bit) noexcept {
    for (const auto& entry : kActionTable) {
        if (entry.name == name) {
            bit = entry.bit;
            return true;
        }
    }
    return false;
}

// Parses "M1=Cross,M2=None,LM=L2" into one output bit per rear button.
// Missing buttons stay 0. Returns false on any unknown key or action name.
[[nodiscard]] inline bool parseRearMap(
    std::string_view spec,
    std::array<std::uint16_t, kRearButtonCount>& bits) noexcept {
    bits.fill(0);
    std::size_t pos = 0;
    while (pos < spec.size()) {
        std::size_t comma = spec.find(',', pos);
        if (comma == std::string_view::npos) comma = spec.size();
        const auto item = spec.substr(pos, comma - pos);
        pos = comma + 1;
        if (item.empty()) continue;

        const auto eq = item.find('=');
        if (eq == std::string_view::npos) return false;
        const auto key = item.substr(0, eq);
        const auto value = item.substr(eq + 1);

        std::size_t index = kRearButtonCount;
        for (std::size_t i = 0; i < kRearButtonCount; ++i) {
            if (kRearButtonNames[i] == key) {
                index = i;
                break;
            }
        }
        std::uint16_t bit = 0;
        if (index == kRearButtonCount || !lookupAction(value, bit)) return false;
        bits[index] = bit;
    }
    return true;
}

} // namespace asb::platform::rear