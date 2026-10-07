#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace asb::dualsense {

enum class FeedbackKind {
    HidOutput,
    AudioHaptics,
    AudioHapticWaveform,
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

    // Eight 1 ms samples per channel, already low-pass filtered and decimated
    // from the virtual DualSense's 48 kHz haptic-audio endpoint.
    std::array<std::int16_t, 8> leftHapticSamples{};
    std::array<std::int16_t, 8> rightHapticSamples{};

    // Optional asb13 diagnostics, measured before 48 kHz -> 1 kHz filtering.
    // Channel order: speaker L/R, haptic L/R. Older backends omit the trailer.
    bool hasRawAudioMeasurements = false;
    std::array<std::uint16_t, 4> rawAudioPeaks{};
    std::uint64_t rawHapticLeftSumSquares = 0;
    std::uint64_t rawHapticRightSumSquares = 0;
    std::uint32_t rawAudioFrames = 0;

    std::uint8_t lightbarRed = 0;
    std::uint8_t lightbarGreen = 0;
    std::uint8_t lightbarBlue = 0;
    bool hasLightbar = false;

    bool hasRumble() const;
    bool requestsRumbleUpdate() const;
    bool hasTriggerEffect() const;
    bool hasLightbarColor() const noexcept;
};

// Decodes the compact server-to-client framing exposed by the patched VIIPER
// DualSense backend. Frame type 0x01 is HID output, 0x02 is audio-haptics
// telemetry, and 0x03 is an 8 ms stereo haptic waveform.
bool decodeViiperFeedbackFrame(std::uint8_t frameType,
                               std::span<const std::uint8_t> payload,
                               DualSenseFeedback& feedback);

} // namespace asb::dualsense
