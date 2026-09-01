#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>

#include "platform/XInputGamepad.h"
#include "platform/XInputMapping.h"

#include <algorithm>
#include <cstdint>

namespace asb::platform {
namespace {

static_assert(xinputButton::kBack == XINPUT_GAMEPAD_BACK);
static_assert(xinputButton::kStart == XINPUT_GAMEPAD_START);
static_assert(xinputButton::kA == XINPUT_GAMEPAD_A);
static_assert(xinputButton::kY == XINPUT_GAMEPAD_Y);
static_assert(xinputButton::kTriggerThreshold == XINPUT_GAMEPAD_TRIGGER_THRESHOLD);

class WindowsXInputGamepad final : public XInputGamepad {
public:
    explicit WindowsXInputGamepad(unsigned int index) : index_(index) {}

    bool poll(dualsense::DualSenseInputState& state, std::string& error) override {
        XINPUT_STATE raw{};
        const auto status = XInputGetState(index_, &raw);
        if (status != ERROR_SUCCESS) {
            error = "XInput controller " + std::to_string(index_) + " disconnected";
            return false;
        }
        const auto& pad = raw.Gamepad;
        state = mapXInputState(XInputSnapshot{
            pad.sThumbLX, pad.sThumbLY, pad.sThumbRX, pad.sThumbRY,
            pad.bLeftTrigger, pad.bRightTrigger, pad.wButtons});
        return true;
    }

    unsigned int index() const noexcept override { return index_; }

private:
    unsigned int index_;
};

struct XInputCapabilitiesEx {
    XINPUT_CAPABILITIES capabilities{};
    WORD vendorId = 0;
    WORD productId = 0;
    WORD productVersion = 0;
    WORD unknown1 = 0;
    DWORD unknown2 = 0;
};

using XInputGetCapabilitiesEx = DWORD (WINAPI*)(
    DWORD, DWORD, DWORD, XInputCapabilitiesEx*);

std::vector<unsigned int> matchingXInputGamepads(
    const std::vector<unsigned int>& connected,
    std::uint16_t vendorId,
    std::uint16_t productId) {
    std::vector<unsigned int> matches;
    HMODULE library = LoadLibraryW(L"xinput1_4.dll");
    if (!library) return matches;
    const auto query = reinterpret_cast<XInputGetCapabilitiesEx>(
        GetProcAddress(library, MAKEINTRESOURCEA(108)));
    if (query) {
        for (const auto index : connected) {
            XInputCapabilitiesEx capabilities{};
            DWORD status = query(1, index, 0, &capabilities);
            if (status != ERROR_SUCCESS) {
                capabilities = {};
                status = query(0, index, 0, &capabilities);
            }
            if (status == ERROR_SUCCESS && capabilities.vendorId == vendorId &&
                capabilities.productId == productId) {
                matches.push_back(index);
            }
        }
    }
    FreeLibrary(library);
    return matches;
}

} // namespace

std::vector<unsigned int> connectedXInputGamepads() {
    std::vector<unsigned int> indices;
    for (unsigned int index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE state{};
        if (XInputGetState(index, &state) == ERROR_SUCCESS) indices.push_back(index);
    }
    return indices;
}

std::unique_ptr<XInputGamepad> openXInputGamepad(
    std::optional<unsigned int> requestedIndex, std::string& error) {
    const auto connected = connectedXInputGamepads();
    unsigned int selected = 0;
    if (requestedIndex) {
        if (std::find(connected.begin(), connected.end(), *requestedIndex) == connected.end()) {
            error = "Requested XInput controller is not connected";
            return {};
        }
        selected = *requestedIndex;
    } else {
        if (connected.empty()) {
            error = "No XInput controller found for the DualSense input proxy";
            return {};
        }
        if (connected.size() != 1) {
            error = "More than one XInput controller found; pass --xinput-index 0..3";
            return {};
        }
        selected = connected.front();
    }
    return std::make_unique<WindowsXInputGamepad>(selected);
}

std::unique_ptr<XInputGamepad> openXInputGamepadForDevice(
    std::uint16_t vendorId,
    std::uint16_t productId,
    std::optional<unsigned int> requestedIndex,
    std::string& error) {
    if (requestedIndex) return openXInputGamepad(requestedIndex, error);

    const auto connected = connectedXInputGamepads();
    if (connected.empty()) {
        error = "No XInput controller found for the DualSense input proxy";
        return {};
    }
    const auto matches = matchingXInputGamepads(connected, vendorId, productId);
    if (matches.size() == 1) {
        return std::make_unique<WindowsXInputGamepad>(matches.front());
    }
    if (matches.size() > 1) {
        error = "More than one XInput controller matches the selected APEX hardware; "
                "use --xinput-index only for diagnostics";
        return {};
    }
    if (connected.size() == 1) {
        return std::make_unique<WindowsXInputGamepad>(connected.front());
    }
    error = "The selected APEX could not be associated with one XInput slot while "
            "multiple controllers are connected";
    return {};
}

} // namespace asb::platform
