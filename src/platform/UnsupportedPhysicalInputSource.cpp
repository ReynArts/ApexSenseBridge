#ifndef _WIN32

#include "platform/PhysicalInputSource.h"

namespace asb::platform {

std::unique_ptr<PhysicalInputSource> openPhysicalInputSource(
    const HidDeviceInfo&, std::optional<unsigned int>, std::string& error) {
    error = "The mandatory physical-input proxy is only available on Windows.";
    return {};
}

} // namespace asb::platform

#endif
