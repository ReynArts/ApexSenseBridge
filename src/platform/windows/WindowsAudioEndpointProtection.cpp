#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <devicetopology.h>
#include <mmdeviceapi.h>
#include <propkey.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include "platform/AudioEndpointProtection.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace asb::platform {
namespace {

using Microsoft::WRL::ComPtr;

constexpr CLSID kPolicyConfigClient{
    0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
constexpr IID kPolicyConfigInterface{
    0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}};

// Windows does not expose SetDefaultEndpoint through a documented public
// interface. This stable PolicyConfig layout is also used by Playnite Audio
// Switcher. Only SetDefaultEndpoint is ever called here.
struct IPolicyConfig : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(LPCWSTR, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(LPCWSTR, BOOL, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(LPCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(LPCWSTR, void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(
        LPCWSTR, BOOL, std::int64_t*, std::int64_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(LPCWSTR, std::int64_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(LPCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(LPCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(
        LPCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(
        LPCWSTR, const PROPERTYKEY&, const PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(LPCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(LPCWSTR, BOOL) = 0;
};

class ComApartment {
public:
    ComApartment() noexcept : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
        uninitialize_ = result_ == S_OK || result_ == S_FALSE;
    }
    ~ComApartment() {
        if (uninitialize_) CoUninitialize();
    }
    [[nodiscard]] bool usable() const noexcept {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }
    [[nodiscard]] HRESULT result() const noexcept { return result_; }

private:
    HRESULT result_;
    bool uninitialize_ = false;
};

struct EndpointInfo {
    std::wstring id;
    std::wstring instanceId;
    std::wstring friendlyName;
};

std::string hresultMessage(std::string_view operation, HRESULT result) {
    std::ostringstream message;
    message << operation << " failed (HRESULT 0x" << std::hex << std::uppercase
            << static_cast<unsigned long>(result) << ").";
    return message.str();
}

std::wstring normalized(std::wstring_view value) {
    std::wstring result(value);
    for (auto& character : result) {
        character = static_cast<wchar_t>(std::towupper(character));
    }
    return result;
}

bool getEndpointId(IMMDevice* device, std::wstring& id, std::string& error) {
    LPWSTR rawId = nullptr;
    const HRESULT result = device->GetId(&rawId);
    if (FAILED(result)) {
        error = hresultMessage("IMMDevice::GetId", result);
        return false;
    }
    id.assign(rawId ? rawId : L"");
    CoTaskMemFree(rawId);
    return !id.empty();
}

std::wstring readStringProperty(IMMDevice* device, const PROPERTYKEY& key) {
    ComPtr<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &properties))) return {};

    PROPVARIANT value;
    PropVariantInit(&value);
    const HRESULT result = properties->GetValue(key, &value);
    std::wstring text;
    if (SUCCEEDED(result) && value.vt == VT_LPWSTR && value.pwszVal) {
        text = value.pwszVal;
    } else if (SUCCEEDED(result) && value.vt == VT_BSTR && value.bstrVal) {
        text = value.bstrVal;
    }
    PropVariantClear(&value);
    return text;
}

std::wstring getAudioControllerInstanceId(
    IMMDevice* endpoint, IMMDeviceEnumerator* enumerator) {
    // Endpoint property stores do not reliably expose PKEY_Device_InstanceId.
    // Resolve the connected topology node first, then read the documented key
    // from that node. This is the same route used by Chromium Core Audio.
    ComPtr<IDeviceTopology> topology;
    if (FAILED(endpoint->Activate(
            __uuidof(IDeviceTopology), CLSCTX_INPROC_SERVER, nullptr,
            reinterpret_cast<void**>(topology.GetAddressOf())))) {
        return {};
    }

    ComPtr<IConnector> connector;
    if (FAILED(topology->GetConnector(0, &connector))) return {};

    LPWSTR connectedDeviceId = nullptr;
    if (FAILED(connector->GetDeviceIdConnectedTo(&connectedDeviceId))) return {};

    ComPtr<IMMDevice> controller;
    const HRESULT result = enumerator->GetDevice(connectedDeviceId, &controller);
    CoTaskMemFree(connectedDeviceId);
    if (FAILED(result)) return {};
    return readStringProperty(controller.Get(), PKEY_Device_InstanceId);
}

bool createEnumerator(ComPtr<IMMDeviceEnumerator>& enumerator, std::string& error) {
    const HRESULT result = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&enumerator));
    if (FAILED(result)) {
        error = hresultMessage("MMDeviceEnumerator creation", result);
        return false;
    }
    return true;
}

bool enumerateActiveRenderEndpoints(
    IMMDeviceEnumerator* enumerator,
    std::vector<EndpointInfo>& endpoints,
    std::string& error,
    const std::unordered_set<std::wstring>* readIdentityForIdsNotIn) {
    endpoints.clear();
    ComPtr<IMMDeviceCollection> collection;
    HRESULT result = enumerator->EnumAudioEndpoints(
        eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(result)) {
        error = hresultMessage("Active playback endpoint enumeration", result);
        return false;
    }

    UINT count = 0;
    result = collection->GetCount(&count);
    if (FAILED(result)) {
        error = hresultMessage("Playback endpoint count", result);
        return false;
    }

    endpoints.reserve(count);
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(index, &device))) continue;

        EndpointInfo endpoint;
        std::string ignored;
        if (!getEndpointId(device.Get(), endpoint.id, ignored)) continue;
        // Capturing the baseline only needs stable endpoint IDs. Walking every
        // endpoint's device topology can block for seconds on dormant Bluetooth
        // or HDMI devices. After DualSense creation, resolve the expensive
        // identity properties only for endpoints absent from that baseline.
        if (readIdentityForIdsNotIn &&
            !readIdentityForIdsNotIn->contains(normalized(endpoint.id))) {
            endpoint.instanceId = getAudioControllerInstanceId(device.Get(), enumerator);
            endpoint.friendlyName = readStringProperty(device.Get(), PKEY_Device_FriendlyName);
        }
        endpoints.push_back(std::move(endpoint));
    }
    return true;
}

bool getDefaultEndpointId(
    IMMDeviceEnumerator* enumerator, ERole role,
    std::wstring& id, std::string& error) {
    ComPtr<IMMDevice> endpoint;
    const HRESULT result = enumerator->GetDefaultAudioEndpoint(eRender, role, &endpoint);
    if (result == E_NOTFOUND || result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
        id.clear();
        return true;
    }
    if (FAILED(result)) {
        error = hresultMessage("Default playback endpoint query", result);
        return false;
    }
    return getEndpointId(endpoint.Get(), id, error);
}

bool createPolicyConfig(ComPtr<IPolicyConfig>& policy, std::string& error) {
    const HRESULT result = CoCreateInstance(
        kPolicyConfigClient, nullptr, CLSCTX_INPROC_SERVER,
        kPolicyConfigInterface, reinterpret_cast<void**>(policy.GetAddressOf()));
    if (FAILED(result)) {
        error = hresultMessage("Windows audio policy client creation", result);
        return false;
    }
    return true;
}

constexpr std::array<ERole, 3> kRoles{
    eConsole, eMultimedia, eCommunications,
};

} // namespace

struct VirtualDualSenseAudioEndpointProtection::Impl {
    std::array<std::wstring, kRoles.size()> defaultEndpointIds{};
    std::unordered_set<std::wstring> activeEndpointIds{};
    AudioDefaultProtectionStatus status = AudioDefaultProtectionStatus::NotCaptured;
    std::size_t restoredRoles = 0;
    bool captured = false;
};

VirtualDualSenseAudioEndpointProtection::VirtualDualSenseAudioEndpointProtection()
    : impl_(std::make_unique<Impl>()) {}
VirtualDualSenseAudioEndpointProtection::~VirtualDualSenseAudioEndpointProtection() = default;

bool VirtualDualSenseAudioEndpointProtection::capture(std::string& error) noexcept {
    impl_->captured = false;
    impl_->restoredRoles = 0;
    impl_->status = AudioDefaultProtectionStatus::NotCaptured;
    impl_->activeEndpointIds.clear();
    impl_->defaultEndpointIds = {};
    error.clear();

    try {
        ComApartment apartment;
        if (!apartment.usable()) {
            impl_->status = AudioDefaultProtectionStatus::Failed;
            error = hresultMessage("COM initialization", apartment.result());
            return false;
        }

        ComPtr<IMMDeviceEnumerator> enumerator;
        if (!createEnumerator(enumerator, error)) {
            impl_->status = AudioDefaultProtectionStatus::Failed;
            return false;
        }

        for (std::size_t index = 0; index < kRoles.size(); ++index) {
            if (!getDefaultEndpointId(
                    enumerator.Get(), kRoles[index],
                    impl_->defaultEndpointIds[index], error)) {
                impl_->status = AudioDefaultProtectionStatus::Failed;
                return false;
            }
        }

        std::vector<EndpointInfo> endpoints;
        if (!enumerateActiveRenderEndpoints(
                enumerator.Get(), endpoints, error, nullptr)) {
            impl_->status = AudioDefaultProtectionStatus::Failed;
            return false;
        }
        for (const auto& endpoint : endpoints) {
            impl_->activeEndpointIds.insert(normalized(endpoint.id));
        }

        const bool hasDefault = std::any_of(
            impl_->defaultEndpointIds.begin(), impl_->defaultEndpointIds.end(),
            [](const auto& id) { return !id.empty(); });
        if (!hasDefault) {
            impl_->status = AudioDefaultProtectionStatus::Failed;
            error = "Windows has no default playback endpoint to preserve.";
            return false;
        }

        impl_->captured = true;
        return true;
    } catch (...) {
        impl_->status = AudioDefaultProtectionStatus::Failed;
        error = "Unexpected failure while capturing the default playback endpoints.";
        return false;
    }
}

bool VirtualDualSenseAudioEndpointProtection::protectAfterVirtualDualSenseStart(
    std::chrono::milliseconds timeout, std::string& error) noexcept {
    error.clear();
    impl_->restoredRoles = 0;
    if (!impl_->captured) {
        impl_->status = AudioDefaultProtectionStatus::Failed;
        error = "The default playback endpoints were not captured before DualSense creation.";
        return false;
    }

    try {
        ComApartment apartment;
        if (!apartment.usable()) {
            impl_->status = AudioDefaultProtectionStatus::Failed;
            error = hresultMessage("COM initialization", apartment.result());
            return false;
        }

        ComPtr<IMMDeviceEnumerator> enumerator;
        if (!createEnumerator(enumerator, error)) {
            impl_->status = AudioDefaultProtectionStatus::Failed;
            return false;
        }

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::optional<std::chrono::steady_clock::time_point> candidateObservedAt;
        std::optional<std::chrono::steady_clock::time_point> lastRestoreAt;
        bool restored = false;

        while (std::chrono::steady_clock::now() <= deadline ||
               (candidateObservedAt &&
                std::chrono::steady_clock::now() <
                    *candidateObservedAt + std::chrono::milliseconds(500))) {
            std::vector<EndpointInfo> endpoints;
            if (!enumerateActiveRenderEndpoints(
                    enumerator.Get(), endpoints, error, &impl_->activeEndpointIds)) {
                impl_->status = AudioDefaultProtectionStatus::Failed;
                return false;
            }

            std::unordered_set<std::wstring> candidates;
            for (const auto& endpoint : endpoints) {
                const auto endpointId = normalized(endpoint.id);
                if (!impl_->activeEndpointIds.contains(endpointId) &&
                    detail::matchesVirtualDualSenseAudioIdentity(
                        endpoint.instanceId, endpoint.friendlyName)) {
                    candidates.insert(endpointId);
                }
            }

            const auto now = std::chrono::steady_clock::now();
            if (!candidates.empty() && !candidateObservedAt) {
                candidateObservedAt = now;
            }

            bool redirected = false;
            if (!candidates.empty()) {
                for (const auto role : kRoles) {
                    std::wstring current;
                    if (!getDefaultEndpointId(enumerator.Get(), role, current, error)) {
                        impl_->status = AudioDefaultProtectionStatus::Failed;
                        return false;
                    }
                    if (candidates.contains(normalized(current))) {
                        redirected = true;
                        break;
                    }
                }
            }

            if (redirected) {
                ComPtr<IPolicyConfig> policy;
                if (!createPolicyConfig(policy, error)) {
                    impl_->status = AudioDefaultProtectionStatus::Failed;
                    return false;
                }
                for (std::size_t index = 0; index < kRoles.size(); ++index) {
                    const auto& previous = impl_->defaultEndpointIds[index];
                    if (previous.empty()) continue;
                    const HRESULT result = policy->SetDefaultEndpoint(
                        previous.c_str(), kRoles[index]);
                    if (FAILED(result)) {
                        impl_->status = AudioDefaultProtectionStatus::Failed;
                        error = hresultMessage("Default playback endpoint restoration", result);
                        return false;
                    }
                }
                if (!restored) {
                    impl_->restoredRoles = static_cast<std::size_t>(std::count_if(
                        impl_->defaultEndpointIds.begin(), impl_->defaultEndpointIds.end(),
                        [](const auto& id) { return !id.empty(); }));
                }
                restored = true;
                lastRestoreAt = now;
            }

            // Remain briefly after endpoint arrival/restoration: Windows can
            // publish the three roles a few milliseconds apart.
            if (candidateObservedAt && !restored &&
                now >= *candidateObservedAt + std::chrono::milliseconds(500)) {
                impl_->status = AudioDefaultProtectionStatus::Unchanged;
                return true;
            }
            if (lastRestoreAt &&
                now >= *lastRestoreAt + std::chrono::milliseconds(250)) {
                impl_->status = AudioDefaultProtectionStatus::Restored;
                return true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        impl_->status = restored
            ? AudioDefaultProtectionStatus::Restored
            : AudioDefaultProtectionStatus::VirtualEndpointNotObserved;
        return true;
    } catch (...) {
        impl_->status = AudioDefaultProtectionStatus::Failed;
        error = "Unexpected failure while protecting the default playback endpoint.";
        return false;
    }
}

bool VirtualDualSenseAudioEndpointProtection::captured() const noexcept {
    return impl_->captured;
}
AudioDefaultProtectionStatus VirtualDualSenseAudioEndpointProtection::status() const noexcept {
    return impl_->status;
}
std::size_t VirtualDualSenseAudioEndpointProtection::restoredRoles() const noexcept {
    return impl_->restoredRoles;
}

} // namespace asb::platform
