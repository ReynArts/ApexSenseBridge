#pragma once

#include "flydigi/Apex5Device.h"

namespace asb {

class TriggerResetGuard {
public:
    explicit TriggerResetGuard(flydigi::Apex5Device& device) noexcept : device_(&device) {}
    ~TriggerResetGuard() noexcept {
        if (device_) {
            std::string ignored;
            device_->clearAll(ignored);
        }
    }

    TriggerResetGuard(const TriggerResetGuard&) = delete;
    TriggerResetGuard& operator=(const TriggerResetGuard&) = delete;

    void dismiss() noexcept { device_ = nullptr; }

private:
    flydigi::Apex5Device* device_;
};

} // namespace asb
