#pragma once

#include <chrono>
#include <cstddef>
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
        std::chrono::milliseconds timeout, std::string& error) noexcept;

    [[nodiscard]] bool captured() const noexcept;
    [[nodiscard]] AudioDefaultProtectionStatus status() const noexcept;
    [[nodiscard]] std::size_t restoredRoles() const noexcept;

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
