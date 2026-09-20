#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace asb::dualsense {

// Byte count of the USB DualSense HID report descriptor.
inline constexpr std::size_t kUsbReportDescriptorSize = 273;

// The exact report descriptor a wired DualSense exposes. Virtual backends must
// publish it verbatim: hid-playstation and the Windows HID stack both identify
// the controller by descriptor shape, not by VID/PID alone.
[[nodiscard]] std::span<const std::uint8_t> usbReportDescriptor() noexcept;

} // namespace asb::dualsense
