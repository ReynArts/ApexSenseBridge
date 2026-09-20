#include "dualsense/VirtualDualSenseStartup.h"

#include <utility>

namespace asb::dualsense {

bool startVerifiedVirtualDualSense(
    VirtualDualSense& backend,
    const DualSenseInputState& initialInput,
    VirtualDualSense::FeedbackHandler feedbackHandler,
    const VirtualDualSenseFirmwareProbe& probeFirmware,
    const VirtualDualSenseRemovalWait& waitForRemoval,
    std::size_t maximumAttempts,
    VirtualDualSenseStartupResult& result,
    std::string& error) {
    result = {};
    error.clear();
    if (maximumAttempts == 0 || !probeFirmware ||
        (maximumAttempts > 1 && !waitForRemoval)) {
        backend.close();
        result.failure = VirtualDualSenseStartupFailure::ReadinessVerification;
        error = maximumAttempts == 0
            ? "Virtual DualSense startup requires at least one attempt."
            : "Virtual DualSense startup is missing a required readiness callback.";
        return false;
    }

    std::string lastVerificationError;
    for (std::size_t attempt = 1; attempt <= maximumAttempts; ++attempt) {
        result.attempts = attempt;

        std::string stageError;
        if (!backend.open(stageError, feedbackHandler)) {
            backend.close();
            result.failure = VirtualDualSenseStartupFailure::BackendOpen;
            error = "Virtual DualSense creation failed";
            if (!stageError.empty()) error += ": " + stageError;
            return false;
        }

        stageError.clear();
        if (!backend.updateInput(initialInput, stageError)) {
            backend.close();
            result.failure = VirtualDualSenseStartupFailure::InitialInput;
            error = "Initial physical-to-DualSense input forwarding failed";
            if (!stageError.empty()) error += ": " + stageError;
            return false;
        }
        result.inputReadyAt = std::chrono::steady_clock::now();

        stageError.clear();
        if (auto readiness = probeFirmware(stageError)) {
            result.firmware = static_cast<const DualSenseFirmwareInfo&>(*readiness);
            result.deviceInfo = std::move(readiness->deviceInfo);
            result.initialReportsValidated = readiness->initialReportsValidated;
            result.verifiedAt = std::chrono::steady_clock::now();
            result.failure = VirtualDualSenseStartupFailure::None;
            error.clear();
            return true;
        }
        lastVerificationError = stageError.empty()
            ? "The virtual DualSense HID firmware interface did not become ready."
            : std::move(stageError);
        backend.close();

        if (attempt == maximumAttempts) break;

        stageError.clear();
        if (!waitForRemoval(stageError)) {
            result.inputReadyAt = {};
            result.deviceInfo.reset();
            result.initialReportsValidated = 0;
            result.failure = VirtualDualSenseStartupFailure::DeviceRemoval;
            error = "The failed virtual DualSense could not be removed before retry";
            if (!stageError.empty()) error += ": " + stageError;
            return false;
        }
    }

    result.inputReadyAt = {};
    result.verifiedAt = {};
    result.deviceInfo.reset();
    result.initialReportsValidated = 0;
    result.failure = VirtualDualSenseStartupFailure::ReadinessVerification;
    error = "Virtual DualSense HID readiness verification failed after " +
            std::to_string(result.attempts) +
            (result.attempts == 1 ? " attempt" : " attempts") + ": " +
            lastVerificationError;
    return false;
}

} // namespace asb::dualsense
