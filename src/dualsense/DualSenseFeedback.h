#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

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

inline constexpr std::uint8_t kUsbOutputReportId = 0x02;
inline constexpr std::uint8_t kBluetoothOutputReportId = 0x31;

// Decodes a raw DualSense output report exactly as a game writes it, for
// backends that expose a real HID device instead of VIIPER's compact framing.
// Accepts the 48-byte descriptor-sized transfer, the 64-byte padded USB form
// and the Bluetooth 0x31 form. Only FeedbackKind::HidOutput is produced; audio
// haptics never travel over HID.
bool decodeDualSenseOutputReport(std::span<const std::uint8_t> report,
                                 DualSenseFeedback& feedback);

// Diagnostics: appends one line describing a decoded output report from the
// game. Writing the decoded form rather than raw bytes keeps the two backends
// comparable - libVIIPER hands us its own frame layout, where the fields sit one
// byte earlier than in the HID report uhid sees, and dumping both raw would make
// every offset in an analysis depend on which backend produced the file.
//
// The translation table can only be extended against what games actually send,
// so this is the file it gets built from.
void appendFeedbackDump(const std::string& path,
                        std::uint64_t microseconds,
                        const DualSenseFeedback& feedback);

} // namespace asb::dualsense
