#pragma once

#include "flydigi/Apex5Device.h"

#include <cstdint>
#include <string>

namespace asb {

// Restores the onboard Apex 5 XInput profile after a temporary session switch.
// The guard is armed before the switch command is sent because the controller
// may apply that command even if another process consumes its acknowledgement.
class ApexProfileRestoreGuard {
public:
    ApexProfileRestoreGuard(flydigi::Apex5Device& device,
                            std::uint8_t originalSlot) noexcept;
    ~ApexProfileRestoreGuard();

    ApexProfileRestoreGuard(const ApexProfileRestoreGuard&) = delete;
    ApexProfileRestoreGuard& operator=(const ApexProfileRestoreGuard&) = delete;

    bool restore(std::string& error) noexcept;
    void dismiss() noexcept { armed_ = false; }
    [[nodiscard]] bool armed() const noexcept { return armed_; }

private:
    flydigi::Apex5Device& device_;
    std::uint8_t originalSlot_ = 0;
    bool armed_ = true;
};

} // namespace asb
