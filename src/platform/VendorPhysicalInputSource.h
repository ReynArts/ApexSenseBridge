#pragma once

#include "core/DeviceInfo.h"
#include "platform/PhysicalInputSource.h"

#include <memory>
#include <string>

namespace asb::platform {

// Both APEX generations carry their complete controller state on the same
// vendor HID interface the bridge already writes effects to, so neither needs
// an XInput or evdev proxy. These sources are portable by construction: they
// run on top of HidTransport and reuse the shared flydigi decoders.
[[nodiscard]] std::unique_ptr<PhysicalInputSource> openApex4VendorInputSource(
    const HidDeviceInfo& vendorInterface, std::string& error);

[[nodiscard]] std::unique_ptr<PhysicalInputSource> openApex5VendorInputSource(
    const HidDeviceInfo& vendorInterface, std::string& error);

} // namespace asb::platform
