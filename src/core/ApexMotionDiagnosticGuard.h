#pragma once

#include "flydigi/Apex5Device.h"
#include "flydigi/Apex5Protocol.h"

#include <optional>
#include <string>

namespace asb {

// Enable the IMU stream for a diagnostic without changing the controller's
// ordinary output or saved profile. Arm before writing: a lost ACK does not
// imply that the firmware ignored the command.
class ApexMotionDiagnosticGuard {
public:
    explicit ApexMotionDiagnosticGuard(flydigi::Apex5Device& device) : device_(device) {}
    ApexMotionDiagnosticGuard(const ApexMotionDiagnosticGuard&) = delete;
    ApexMotionDiagnosticGuard& operator=(const ApexMotionDiagnosticGuard&) = delete;
    ~ApexMotionDiagnosticGuard() {
        std::string ignored;
        (void)restore(ignored);
    }

    bool enable(std::string& error) {
        if (!device_.identity() || !device_.identity()->isApex5()) return true;
        flydigi::InputTransportStatus original{};
        if (!device_.readInputTransportStatus(original, error)) return false;
        if (original.rawData) return true;
        if (original.thirdPartyControl) {
            error = "Another application owns the Apex 5 input transport.";
            return false;
        }
        original_ = original;
        return device_.setInputTransport(original.controllerData, true, error);
    }

    bool restore(std::string& error) noexcept {
        if (!original_) return true;
        try {
            for (int attempt = 0; attempt < 3; ++attempt) {
                if (device_.setInputTransport(original_->controllerData,
                                              original_->rawData, error)) {
                    original_.reset();
                    return true;
                }
            }
        } catch (...) {
            error = "Unexpected failure restoring the Apex 5 motion routing.";
        }
        return false;
    }

private:
    flydigi::Apex5Device& device_;
    std::optional<flydigi::InputTransportStatus> original_;
};

} // namespace asb
