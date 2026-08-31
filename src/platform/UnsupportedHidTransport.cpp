#ifndef _WIN32

#include "platform/HidTransport.h"

namespace asb::platform {

std::vector<HidDeviceInfo> enumerateHidDevices(std::string& error) {
    error = "HID hardware access in v0.2 is implemented for Windows only";
    return {};
}

HidTransport* createHidTransport(const HidDeviceInfo&, std::string& error) {
    error = "HID hardware access in v0.2 is implemented for Windows only";
    return nullptr;
}

void destroyHidTransport(HidTransport* transport) noexcept {
    delete transport;
}

} // namespace asb::platform

#endif
