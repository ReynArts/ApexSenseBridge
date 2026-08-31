#pragma once

#include <cstdint>

namespace asb {

enum class TriggerSide : std::uint8_t {
    Left = 1,
    Right = 2,
};

enum class TriggerMode : std::uint8_t {
    Normal = 0,
    Race = 1,
    RecoilRattle = 2,   // Wire enum: Sniper; Space Station UI/physical behaviour: recoil/rattle.
    SniperBreak = 3,    // Wire enum: Recoil; Space Station UI/physical behaviour: breakthrough.
    Lock = 4,
    Vibration = 5,
};

struct TriggerEffect {
    TriggerSide side = TriggerSide::Right;
    TriggerMode mode = TriggerMode::Normal;

    // Travel starts at 0 and is normally capped around 192 by Space Station.
    std::uint8_t start = 0;

    // Generic effect parameters. Their meaning depends on mode.
    std::uint8_t p1 = 1;
    std::uint8_t p2 = 1;
    std::uint8_t p3 = 1;
    bool matchInput = false;
};

} // namespace asb
