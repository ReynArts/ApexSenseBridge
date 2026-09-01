#include "platform/AudioEndpointProtection.h"

#include <cwctype>
#include <string>

namespace asb::platform {
namespace {

std::wstring upperAscii(std::wstring_view value) {
    std::wstring result(value);
    for (auto& character : result) {
        character = static_cast<wchar_t>(std::towupper(character));
    }
    return result;
}

} // namespace

const char* audioDefaultProtectionStatusName(
    AudioDefaultProtectionStatus status) noexcept {
    switch (status) {
    case AudioDefaultProtectionStatus::NotCaptured:
        return "not-captured";
    case AudioDefaultProtectionStatus::Unchanged:
        return "unchanged";
    case AudioDefaultProtectionStatus::Restored:
        return "restored";
    case AudioDefaultProtectionStatus::VirtualEndpointNotObserved:
        return "virtual-endpoint-not-observed";
    case AudioDefaultProtectionStatus::Failed:
        return "failed";
    }
    return "unknown";
}

namespace detail {

bool matchesVirtualDualSenseAudioIdentity(
    std::wstring_view deviceInstanceId,
    std::wstring_view friendlyName) noexcept {
    const auto instance = upperAscii(deviceInstanceId);
    if (instance.find(L"VID_054C&PID_0CE6&MI_00") != std::wstring::npos) {
        return true;
    }

    // Some Windows audio endpoint property stores omit the USB instance ID.
    // The caller also requires the endpoint to be new relative to the snapshot,
    // so this fallback cannot select an already-connected physical controller.
    return upperAscii(friendlyName) == L"WIRELESS CONTROLLER";
}

} // namespace detail
} // namespace asb::platform
