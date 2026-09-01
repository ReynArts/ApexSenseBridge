#pragma once

#include "flydigi/Apex5Device.h"

namespace asb {

class RumbleResetGuard {
public:
    explicit RumbleResetGuard(flydigi::Apex5Device& device) noexcept : device_(&device) {}
    ~RumbleResetGuard() noexcept {
        if (device_) {
            std::string ignored;
            device_->stopRumble(ignored);
        }
    }

    RumbleResetGuard(const RumbleResetGuard&) = delete;
    RumbleResetGuard& operator=(const RumbleResetGuard&) = delete;

    void dismiss() noexcept { device_ = nullptr; }

private:
    flydigi::Apex5Device* device_;
};

} // namespace asb
