#include "platform/AudioEndpointProtection.h"

namespace asb::platform {

struct VirtualDualSenseAudioEndpointProtection::Impl {
    AudioDefaultProtectionStatus status = AudioDefaultProtectionStatus::NotCaptured;
};

VirtualDualSenseAudioEndpointProtection::VirtualDualSenseAudioEndpointProtection()
    : impl_(std::make_unique<Impl>()) {}
VirtualDualSenseAudioEndpointProtection::~VirtualDualSenseAudioEndpointProtection() = default;

bool VirtualDualSenseAudioEndpointProtection::capture(std::string& error) noexcept {
    impl_->status = AudioDefaultProtectionStatus::Failed;
    error = "Default-audio protection is only available on Windows.";
    return false;
}

bool VirtualDualSenseAudioEndpointProtection::protectAfterVirtualDualSenseStart(
    std::chrono::milliseconds, std::string& error) noexcept {
    impl_->status = AudioDefaultProtectionStatus::Failed;
    error = "Default-audio protection is only available on Windows.";
    return false;
}

bool VirtualDualSenseAudioEndpointProtection::captured() const noexcept { return false; }
AudioDefaultProtectionStatus VirtualDualSenseAudioEndpointProtection::status() const noexcept {
    return impl_->status;
}
std::size_t VirtualDualSenseAudioEndpointProtection::restoredRoles() const noexcept {
    return 0;
}

} // namespace asb::platform
