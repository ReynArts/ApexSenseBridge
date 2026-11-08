#pragma once

#include "flydigi/Apex5Device.h"
#include "flydigi/Apex5Protocol.h"
#include "flydigi/Apex6Imu.h"

#include <chrono>
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
        if (device_.identity() && device_.identity()->isApex6()) {
            return enableApex6(error);
        }
        if (!device_.identity() || !device_.identity()->isApex5()) return true;
        flydigi::InputTransportStatus original{};
        if (!device_.readInputTransportStatus(original, error)) return false;
        if (original.rawData) return true;
        if (original.thirdPartyControl.value().value_or(false)) {
            error = "Another application owns the Apex 5 input transport.";
            return false;
        }
        original_ = original;
        return device_.setInputTransport(original.controllerData, true, error);
    }

    bool restore(std::string& error) noexcept {
        if (apex6Armed_) return restoreApex6(error);
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
    static constexpr auto kApex6ExistingStreamWindow = std::chrono::milliseconds(150);
    static constexpr auto kApex6StartWindow = std::chrono::milliseconds(1500);

    bool enableApex6(std::string& error) {
        if (!flydigi::apex6::hasImu(device_.identity()->features())) {
            error = "This Apex 6 does not advertise an IMU (GetInfo capability bit 0 is clear)";
            return false;
        }
        flydigi::apex6::TransportStatus status{};
        if (!device_.readApex6TransportStatus(status, error)) return false;
        if (status.takeoverActive()) {
            error = "Another application owns the Apex 6 input transport.";
            return false;
        }
        std::string streamError;
        if (device_.apex6OperatorStreamActive(kApex6ExistingStreamWindow, streamError)) {
            return true;
        }
        apex6Armed_ = true;
        if (!device_.setApex6RawDataRouting(true, error)) return false;
        if (!device_.apex6OperatorStreamActive(kApex6StartWindow, streamError)) {
            error = streamError.empty()
                ? "The Apex 6 accepted the raw-data routing command but produced no "
                  "operator report within 1.5 s."
                : streamError;
            return false;
        }
        return true;
    }

    bool restoreApex6(std::string& error) noexcept {
        try {
            for (int attempt = 0; attempt < 3; ++attempt) {
                if (device_.setApex6RawDataRouting(false, error)) {
                    apex6Armed_ = false;
                    return true;
                }
            }
        } catch (...) {
            error = "Unexpected failure restoring the Apex 6 motion routing.";
        }
        return false;
    }

    flydigi::Apex5Device& device_;
    std::optional<flydigi::InputTransportStatus> original_;
    bool apex6Armed_ = false;
};

} // namespace asb
