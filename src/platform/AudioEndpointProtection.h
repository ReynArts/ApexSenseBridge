#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace asb::platform {

enum class AudioDefaultProtectionStatus {
    NotCaptured,
    Unchanged,
    Restored,
    VirtualEndpointNotObserved,
    Failed,
};

[[nodiscard]] const char* audioDefaultProtectionStatusName(
    AudioDefaultProtectionStatus status) noexcept;

enum class HapticAudioFormatStatus {
    NotRequested,
    NotObserved,
    Unknown,
    Quadraphonic,
    NeedsConfiguration,
};

struct HapticAudioFormat {
    HapticAudioFormatStatus status = HapticAudioFormatStatus::NotRequested;
    std::uint16_t channels = 0;
    std::uint32_t channelMask = 0;
    std::uint32_t physicalSpeakerMask = 0;
};

[[nodiscard]] const char* hapticAudioFormatStatusName(HapticAudioFormatStatus status) noexcept;
[[nodiscard]] HapticAudioFormatStatus classifyHapticAudioFormat(
    std::uint16_t channels, std::uint32_t channelMask,
    std::uint32_t physicalSpeakerMask) noexcept;

// Takes a snapshot before VIIPER creates its virtual DualSense, then restores
// only roles that Windows redirected to the newly-created controller endpoint.
// The endpoint itself stays enabled so games can continue to send haptic audio.
class VirtualDualSenseAudioEndpointProtection {
public:
    VirtualDualSenseAudioEndpointProtection();
    ~VirtualDualSenseAudioEndpointProtection();

    VirtualDualSenseAudioEndpointProtection(
        const VirtualDualSenseAudioEndpointProtection&) = delete;
    VirtualDualSenseAudioEndpointProtection& operator=(
        const VirtualDualSenseAudioEndpointProtection&) = delete;

    bool capture(std::string& error) noexcept;
    bool protectAfterVirtualDualSenseStart(
        std::chrono::milliseconds timeout, std::string& error,
        bool inspectHapticFormat = false) noexcept;

    [[nodiscard]] bool captured() const noexcept;
    [[nodiscard]] AudioDefaultProtectionStatus status() const noexcept;
    [[nodiscard]] std::size_t restoredRoles() const noexcept;
    // Read-only inspection, requested only by the APEX 6 native PCM path.
    [[nodiscard]] HapticAudioFormat hapticFormat() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

namespace detail {

// Kept platform-independent so the strict virtual-audio identity guard can be
// unit-tested without reading or changing the machine's audio configuration.
[[nodiscard]] bool matchesVirtualDualSenseAudioIdentity(
    std::wstring_view deviceInstanceId,
    std::wstring_view friendlyName) noexcept;

} // namespace detail

} // namespace asb::platform
