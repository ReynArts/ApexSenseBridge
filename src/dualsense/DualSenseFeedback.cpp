#include "dualsense/DualSenseFeedback.h"

#include <algorithm>

namespace asb::dualsense {
namespace {

std::uint16_t readU16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) |
           (static_cast<std::uint16_t>(data[offset + 1]) << 8);
}

std::uint32_t readU32(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::uint64_t readU64(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint64_t>(readU32(data, offset)) |
           (static_cast<std::uint64_t>(readU32(data, offset + 4)) << 32);
}

} // namespace

bool DualSenseFeedback::hasRumble() const {
    return kind == FeedbackKind::HidOutput && (rumbleLeft != 0 || rumbleRight != 0);
}

bool DualSenseFeedback::requestsRumbleUpdate() const {
    constexpr std::uint8_t kCompatibleVibration = 0x01;
    constexpr std::uint8_t kCompatibleVibration2 = 0x04;
    return kind == FeedbackKind::HidOutput &&
           (((enableBits1 & kCompatibleVibration) != 0) ||
            ((enableBits3 & kCompatibleVibration2) != 0));
}

bool DualSenseFeedback::hasTriggerEffect() const {
    if (kind != FeedbackKind::HidOutput) {
        return false;
    }
    const auto nonZero = [](std::uint8_t value) { return value != 0; };
    return std::any_of(leftTriggerEffect.begin(), leftTriggerEffect.end(), nonZero) ||
           std::any_of(rightTriggerEffect.begin(), rightTriggerEffect.end(), nonZero);
}

bool DualSenseFeedback::hasLightbarColor() const noexcept {
    return kind == FeedbackKind::HidOutput && hasLightbar;
}

bool decodeViiperFeedbackFrame(std::uint8_t frameType,
                               std::span<const std::uint8_t> payload,
                               DualSenseFeedback& feedback) {
    if (frameType == 0x01) {
        if (payload.size() < 27) {
            return false;
        }

        // The ASB VIIPER extension normally emits a compact 27-byte payload.
        // WebHID can also write the complete DualSense USB output body: the
        // trigger blocks then begin at offsets 10 and 21 (or at 12 and 23 for
        // the Bluetooth body). Accept both forms so browser test tools and
        // native games use the same feedback path.
        std::size_t controlOffset = 0;
        std::size_t triggerOffset = 5;
        std::size_t lightbarOffset = 27;
        if (payload.size() >= 77 && payload[1] == 16) {
            controlOffset = 2;
            triggerOffset = 12;
            lightbarOffset = 46;
        } else if (payload.size() >= 78 && payload[2] == 16) {
            controlOffset = 3;
            triggerOffset = 13;
            lightbarOffset = 47;
        } else if (payload.size() >= 63 && payload[0] == 0x02) {
            controlOffset = 1;
            triggerOffset = 11;
            lightbarOffset = 45;
        } else if (payload.size() >= 62) {
            controlOffset = 0;
            triggerOffset = 10;
            lightbarOffset = 44;
        }
        if (triggerOffset + 22 > payload.size()) {
            return false;
        }

        DualSenseFeedback decoded{};
        decoded.kind = FeedbackKind::HidOutput;
        decoded.enableBits1 = payload[controlOffset];
        decoded.enableBits2 = payload[controlOffset + 1];
        decoded.rumbleRight = payload[controlOffset + 2];
        decoded.rumbleLeft = payload[controlOffset + 3];
        decoded.enableBits3 = payload[controlOffset + 4];
        std::copy_n(payload.begin() + triggerOffset, decoded.rightTriggerEffect.size(),
                    decoded.rightTriggerEffect.begin());
        std::copy_n(payload.begin() + triggerOffset + 11, decoded.leftTriggerEffect.size(),
                    decoded.leftTriggerEffect.begin());
        constexpr std::uint8_t kLightbarControlEnable = 0x04;
        if (payload.size() >= lightbarOffset + 3 &&
            (decoded.enableBits2 & kLightbarControlEnable) != 0) {
            decoded.lightbarRed = payload[lightbarOffset];
            decoded.lightbarGreen = payload[lightbarOffset + 1];
            decoded.lightbarBlue = payload[lightbarOffset + 2];
            decoded.hasLightbar = true;
        }
        feedback = decoded;
        return true;
    }

    if (frameType == 0x02) {
        if (payload.size() < 16) {
            return false;
        }

        DualSenseFeedback decoded{};
        decoded.kind = FeedbackKind::AudioHaptics;
        decoded.audioSequence = readU32(payload, 0);
        decoded.leftEnergy = readU16(payload, 4);
        decoded.rightEnergy = readU16(payload, 6);
        decoded.leftPeak = readU16(payload, 8);
        decoded.rightPeak = readU16(payload, 10);
        decoded.leftTransient = readU16(payload, 12);
        decoded.rightTransient = readU16(payload, 14);
        feedback = decoded;
        return true;
    }

    if (frameType == 0x03) {
        constexpr std::size_t kPayloadSize = 4 + 8 * 2 * sizeof(std::int16_t);
        if (payload.size() < kPayloadSize) {
            return false;
        }

        DualSenseFeedback decoded{};
        decoded.kind = FeedbackKind::AudioHapticWaveform;
        decoded.audioSequence = readU32(payload, 0);
        for (std::size_t index = 0; index < 8; ++index) {
            decoded.leftHapticSamples[index] = static_cast<std::int16_t>(
                readU16(payload, 4 + index * 4));
            decoded.rightHapticSamples[index] = static_cast<std::int16_t>(
                readU16(payload, 6 + index * 4));
        }
        if (payload.size() >= 64) {
            decoded.hasRawAudioMeasurements = true;
            for (std::size_t index = 0; index < decoded.rawAudioPeaks.size(); ++index) {
                decoded.rawAudioPeaks[index] = readU16(payload, 36 + index * 2);
            }
            decoded.rawHapticLeftSumSquares = readU64(payload, 44);
            decoded.rawHapticRightSumSquares = readU64(payload, 52);
            decoded.rawAudioFrames = readU32(payload, 60);
        }
        feedback = decoded;
        return true;
    }

    return false;
}

} // namespace asb::dualsense
