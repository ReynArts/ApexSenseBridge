#pragma once

#include "dualsense/DualSenseInput.h"

#include <cstdint>

namespace asb::tools {

struct PositionMarkerEdges {
    bool onset = false;
    bool release = false;
};

class TriggerPositionMarkers {
public:
    explicit TriggerPositionMarkers(std::uint16_t initialButtons) : previousButtons_(initialButtons) {}

    PositionMarkerEdges update(std::uint16_t buttons, bool active) noexcept {
        const auto pressed = static_cast<std::uint16_t>(buttons & ~previousButtons_);
        previousButtons_ = buttons;
        return {active && (pressed & dualsense::button::kCross) != 0,
                active && (pressed & dualsense::button::kCircle) != 0};
    }

private:
    std::uint16_t previousButtons_;
};

class MarkerTrialProgress {
public:
    explicit MarkerTrialProgress(bool requireRelease) : requireRelease_(requireRelease) {}

    bool update(PositionMarkerEdges edges) noexcept {
        const bool releaseAfterOnset = edges.release && onsetSeen_ && !edges.onset;
        if (edges.onset) onsetSeen_ = true;
        return requireRelease_ ? releaseAfterOnset : edges.onset;
    }

private:
    bool requireRelease_;
    bool onsetSeen_ = false;
};

}
