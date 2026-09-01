#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace asb::dualsense {

enum class FeedbackKind {
    HidOutput,
    AudioHaptics,
};

struct DualSenseFeedback {
    FeedbackKind kind = FeedbackKind::HidOutput;

    std::uint8_t enableBits1 = 0;
    std::uint8_t enableBits2 = 0;
    std::uint8_t enableBits3 = 0;
    std::uint8_t rumbleRight = 0;
    std::uint8_t rumbleLeft = 0;
    std::array<std::uint8_t, 11> rightTriggerEffect{};
    std::array<std::uint8_t, 11> leftTriggerEffect{};

    std::uint32_t audioSequence = 0;
    std::uint16_t leftEnergy = 0;
    std::uint16_t rightEnergy = 0;
    std::uint16_t leftPeak = 0;
    std::uint16_t rightPeak = 0;
    std::uint16_t leftTransient = 0;
    std::uint16_t rightTransient = 0;

    bool hasRumble() const;
    bool requestsRumbleUpdate() const;
    bool hasTriggerEffect() const;
};

// Decodes the compact server-to-client framing exposed by the patched VIIPER
// DualSense backend. Frame type 0x01 is HID output and 0x02 is audio haptics.
bool decodeViiperFeedbackFrame(std::uint8_t frameType,
                               std::span<const std::uint8_t> payload,
                               DualSenseFeedback& feedback);

} // namespace asb::dualsense
