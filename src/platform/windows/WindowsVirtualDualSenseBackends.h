#pragma once

#include "dualsense/VirtualDualSense.h"

#include <memory>

namespace asb::dualsense {

std::unique_ptr<VirtualDualSense> createLibViiperVirtualDualSense(
    VirtualDualSenseOptions options);

} // namespace asb::dualsense
