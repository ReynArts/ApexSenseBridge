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

} // namespace

bool DualSenseFeedback::hasRumble() const {
    return kind == FeedbackKind::HidOutput && (rumbleLeft != 0 || rumbleRight != 0);
}

bool DualSenseFeedback::requestsRumbleUpdate() const {
    constexpr std::uint8_t kCompatibleVibration = 0x01;
    constexpr std::uint8_t kHapticsSelect = 0x02;
    constexpr std::uint8_t kCompatibleVibration2 = 0x04;
    return kind == FeedbackKind::HidOutput &&
           (((enableBits1 & (kCompatibleVibration | kHapticsSelect)) != 0) ||
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

bool decodeViiperFeedbackFrame(std::uint8_t frameType,
                               std::span<const std::uint8_t> payload,
                               DualSenseFeedback& feedback) {
    if (frameType == 0x01) {
        if (payload.size() < 27) {
            return false;
        }

        DualSenseFeedback decoded{};
        decoded.kind = FeedbackKind::HidOutput;
        decoded.enableBits1 = payload[0];
        decoded.enableBits2 = payload[1];
        decoded.rumbleRight = payload[2];
        decoded.rumbleLeft = payload[3];
        decoded.enableBits3 = payload[4];
        std::copy_n(payload.begin() + 5, decoded.rightTriggerEffect.size(),
                    decoded.rightTriggerEffect.begin());
        std::copy_n(payload.begin() + 16, decoded.leftTriggerEffect.size(),
                    decoded.leftTriggerEffect.begin());
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

    return false;
}

} // namespace asb::dualsense
