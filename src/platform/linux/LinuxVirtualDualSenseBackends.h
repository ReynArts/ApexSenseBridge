#pragma once

#include "dualsense/VirtualDualSense.h"

#include <filesystem>
#include <memory>

namespace asb::dualsense {

// The path the libVIIPER backend will try to load. Exposed so the backend
// chooser can ask the question ahead of time and get the same answer open()
// will reach, instead of a second guess at the same rule.
[[nodiscard]] std::filesystem::path resolveLibViiperLibraryPath(
    const VirtualDualSenseOptions& options);

// Publishes the controller through the kernel's uhid interface. Needs no
// privileges and no kernel modules beyond uhid itself, but can only ever be a
// HID device: it has no USB topology and no audio endpoint.
std::unique_ptr<VirtualDualSense> createUhidVirtualDualSense(
    VirtualDualSenseOptions options);

// Publishes the controller as a real USB device through libVIIPER and the
// kernel's vhci-hcd, the same library the Windows build uses. Gains a genuine
// usb_device ancestor and the four-channel audio endpoint DualSense haptics
// travel on, at the cost of needing usbip(8) and root to attach.
std::unique_ptr<VirtualDualSense> createLibViiperVirtualDualSense(
    VirtualDualSenseOptions options);

} // namespace asb::dualsense
