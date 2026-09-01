#include "platform/XInputGamepad.h"

namespace asb::platform {

std::vector<unsigned int> connectedXInputGamepads() { return {}; }

std::unique_ptr<XInputGamepad> openXInputGamepad(
    std::optional<unsigned int>, std::string& error) {
    error = "XInput proxy is only available on Windows";
    return {};
}

std::unique_ptr<XInputGamepad> openXInputGamepadForDevice(
    std::uint16_t, std::uint16_t,
    std::optional<unsigned int> requestedIndex, std::string& error) {
    return openXInputGamepad(requestedIndex, error);
}

} // namespace asb::platform
