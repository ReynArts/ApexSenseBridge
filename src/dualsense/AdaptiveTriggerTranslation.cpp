#include "dualsense/AdaptiveTriggerTranslation.h"

#include <algorithm>

namespace asb::dualsense {
namespace {

ForceTriggerCommand make(TriggerSide side, TriggerMode mode,
                         std::array<std::uint8_t, 5> params = {}) {
    return ForceTriggerCommand{side, mode, params};
}

} // namespace

std::optional<ForceTriggerCommand> translateAdaptiveTrigger(
    TriggerSide side, const std::array<std::uint8_t, 11>& effect,
    std::uint8_t leftMotor) {
    const auto type = effect[0];
    const auto* p = effect.data() + 1;

    if (type == 1) return make(side, TriggerMode::Race, {p[0], p[1], 0, 0, 0});
    if (type == 2) return make(side, TriggerMode::SniperBreak, {p[0], p[1], p[2], 0, 0});
    if (type == 5) return make(side, TriggerMode::Normal);
    if (type == 6) return make(side, TriggerMode::RecoilRattle, {p[2], p[1], p[1], p[0], 0});

    if (side == TriggerSide::Right) {
        if (type == 33) {
            if (p[0] == 0xFF && p[1] == 3 && p[2] == 0xFF)
                return make(side, TriggerMode::Race, {110, 50, 0, 0, 0});
            if (p[0] == 0) return make(side, TriggerMode::Race, {120, 1, 0, 0, 0});
            if (p[0] == 0xFF && p[1] == 3)
                return make(side, TriggerMode::Race, {1, 64, 0, 0, 0});
            return make(side, TriggerMode::Race, {1, 1, 0, 0, 0});
        }
        if (type == 37) {
            if (p[0] == 20) {
                if (p[2] == 2) return make(side, TriggerMode::SniperBreak, {70, 20, 20, 0, 0});
                if (p[2] == 6) return make(side, TriggerMode::SniperBreak, {70, 60, 20, 0, 0});
                if (p[2] == 1) return make(side, TriggerMode::SniperBreak, {20, 10, 20, 0, 0});
                if (p[2] == 3) return make(side, TriggerMode::SniperBreak, {50, 30, 1, 0, 1});
                return make(side, TriggerMode::RecoilRattle, {50, 1, 10, 10, 10});
            }
            if (p[0] == 12) return make(side, TriggerMode::SniperBreak, {70, 0, 12, 0, 0});
            if (p[0] == 36 && p[2] <= 6)
                return make(side, TriggerMode::SniperBreak,
                            {10, 36, static_cast<std::uint8_t>(10 + p[2] * 10), 0, 0});
            if (p[0] == 68) return make(side, TriggerMode::SniperBreak, {70, 50, 68, 0, 0});
            if (p[0] == 4 && p[1] == 1 && (p[2] == 5 || p[2] == 7))
                return make(side, TriggerMode::SniperBreak, {80, 200, 90, 0, 0});
            if (p[0] == 64 && p[1] == 1 && p[2] == 3)
                return make(side, TriggerMode::SniperBreak, {120, 150, 60, 0, 0});
            return make(side, TriggerMode::SniperBreak, {64, p[0], 0, p[2], 1});
        }
        if (type == 38)
            return make(side, TriggerMode::RecoilRattle,
                        {static_cast<std::uint8_t>(255 - p[0]), 1,
                         static_cast<std::uint8_t>((p[1] + 1) * 30), p[8], 0});
        return std::nullopt;
    }

    if (type == 33) {
        std::array<std::uint8_t, 5> out{};
        if (p[0] == 0) out = {120, 1, 0, 0, 0};
        else if (p[0] == 252 || (p[0] == 192 && p[1] == 3)) out = {1, 96, 0, 0, 0};
        else out = {0, 1, 0, 0, 0};
        if (p[1] == 3) { out[0] = 140; out[1] = static_cast<std::uint8_t>(p[5] + 1); }
        if (p[0] == 128) { out[0] = 128; out[1] = p[4]; }
        return make(side, TriggerMode::Race, out);
    }
    if (type == 37)
        return make(side, TriggerMode::SniperBreak, {64, p[0], 0, p[2], 1});
    if (type == 38) {
        if (p[0] == 240 && p[1] == 3 && p[3] == 0)
            return make(side, TriggerMode::Race, {30, leftMotor, 0, 0, 0});
        if (p[0] == 0xFF && p[1] == 3 && p[3] == 0xFF) return std::nullopt;
        const auto strength = p[2] == 0
            ? static_cast<std::uint8_t>((p[1] + 1) * 30)
            : std::max({p[2], p[3], p[4], p[5]});
        return make(side, TriggerMode::RecoilRattle,
                    {static_cast<std::uint8_t>(255 - p[0]), 1, strength, p[8], 0});
    }
    return std::nullopt;
}

} // namespace asb::dualsense
