#pragma once

#include "dualsense/VirtualDualSense.h"

#include <memory>

namespace asb::dualsense {

bool libViiperCleanupFailed() noexcept;

std::unique_ptr<VirtualDualSense> createLibViiperVirtualDualSense(
    VirtualDualSenseOptions options);

} // namespace asb::dualsense
