#pragma once

#include "core/DeviceInfo.h"
#include "dualsense/DualSenseFirmware.h"
#include "dualsense/DualSenseInput.h"
#include "dualsense/VirtualDualSense.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asb::dualsense {

enum class VirtualDualSenseStartupFailure {
    None,
    BackendOpen,
    InitialInput,
    ReadinessVerification,
    DeviceRemoval,
    Cancelled,
};

struct VirtualDualSenseReadiness : public DualSenseFirmwareInfo {
    std::optional<HidDeviceInfo> deviceInfo{};
    std::size_t initialReportsValidated = 0;

    VirtualDualSenseReadiness() = default;
    VirtualDualSenseReadiness(const DualSenseFirmwareInfo& fw)
        : DualSenseFirmwareInfo(fw) {}
    VirtualDualSenseReadiness(const DualSenseFirmwareInfo& fw,
                              std::optional<HidDeviceInfo> dev,
                              std::size_t validated = 0)
        : DualSenseFirmwareInfo(fw), deviceInfo(std::move(dev)), initialReportsValidated(validated) {}
};

[[nodiscard]] bool isDualSenseHidInterface(const HidDeviceInfo& info) noexcept;
[[nodiscard]] bool isDualSenseGamepadCollection(const HidDeviceInfo& info) noexcept;

class VirtualDualSenseBaseline {
public:
    VirtualDualSenseBaseline() = default;
    explicit VirtualDualSenseBaseline(std::vector<std::wstring> paths);

    void reset(std::vector<std::wstring> paths);
    void observe(const std::vector<HidDeviceInfo>& devices);
    [[nodiscard]] bool contains(std::wstring_view path) const;
    [[nodiscard]] bool wasPresentInitially(std::wstring_view path) const;
    [[nodiscard]] std::size_t initialCount() const noexcept { return initial_.size(); }
    [[nodiscard]] std::size_t activeCount() const noexcept { return active_.size(); }
    [[nodiscard]] std::size_t vanishedCount() const noexcept { return vanished_.size(); }
    [[nodiscard]] std::size_t reappearedCount() const noexcept { return reappeared_.size(); }

private:
    std::vector<std::wstring> initial_;
    std::vector<std::wstring> active_;
    std::vector<std::wstring> vanished_;
    std::vector<std::wstring> reappeared_;
};

struct VirtualDualSenseProbeTiming {
    std::chrono::milliseconds quietWindow{4000};
    std::chrono::milliseconds progressWindow{6500};
    std::chrono::milliseconds pollInterval{50};
};

enum class VirtualDualSenseProbeOutcome {
    Ready,
    NotFound,
    CapabilitiesUnavailable,
    UnexpectedInterface,
    CandidateRejected,
    EnumerationFailed,
    Cancelled,
};

[[nodiscard]] std::string_view virtualDualSenseProbeOutcomeName(
    VirtualDualSenseProbeOutcome outcome) noexcept;

struct VirtualDualSenseProbeInterface {
    std::string path;
    std::string interfaceNumber;
    std::uint16_t usagePage = 0;
    std::uint16_t usage = 0;
    std::uint16_t inputReportLength = 0;
    std::uint16_t featureReportLength = 0;
    std::string state;
    std::int64_t firstSeenMilliseconds = -1;
    std::string detail;
};

struct VirtualDualSenseProbeReport {
    std::size_t attempt = 0;
    VirtualDualSenseProbeOutcome outcome = VirtualDualSenseProbeOutcome::NotFound;
    std::int64_t elapsedMilliseconds = 0;
    std::int64_t deadlineMilliseconds = 0;
    bool extended = false;
    std::int64_t firstNewInterfaceMilliseconds = -1;
    std::size_t enumerations = 0;
    std::size_t enumerationFailures = 0;
    std::size_t baselineInterfaces = 0;
    std::size_t baselineVanished = 0;
    std::size_t baselineReappeared = 0;
    std::size_t newGamepadInterfaces = 0;
    std::size_t newUnreadableInterfaces = 0;
    std::size_t newOtherInterfaces = 0;
    std::size_t candidateFailures = 0;
    std::string lastError;
    std::vector<VirtualDualSenseProbeInterface> interfaces;
};

[[nodiscard]] std::string describeVirtualDualSenseProbe(const VirtualDualSenseProbeReport& report);

struct VirtualDualSenseProbeEnvironment {
    std::function<std::vector<HidDeviceInfo>(std::string& error)> enumerate;
    std::function<std::optional<VirtualDualSenseReadiness>(const HidDeviceInfo& candidate,
                                                           std::string& error)> verify;
    std::function<std::string(const HidDeviceInfo& candidate)> explainUnreadable;
    std::function<bool()> stopRequested;
    std::function<std::chrono::steady_clock::time_point()> now;
    std::function<void(std::chrono::milliseconds)> sleep;
};

std::optional<VirtualDualSenseReadiness> probeNewVirtualDualSense(
    VirtualDualSenseBaseline& baseline,
    const VirtualDualSenseProbeTiming& timing,
    const VirtualDualSenseProbeEnvironment& environment,
    VirtualDualSenseProbeReport& report,
    std::string& error);

bool waitForVirtualDualSenseRemoval(
    VirtualDualSenseBaseline& baseline,
    const VirtualDualSenseProbeEnvironment& environment,
    std::chrono::milliseconds timeout,
    std::chrono::milliseconds stableAbsence,
    std::string& error);

struct VirtualDualSenseStartupResult {
    std::optional<DualSenseFirmwareInfo> firmware;
    std::optional<HidDeviceInfo> deviceInfo;
    std::size_t initialReportsValidated = 0;
    std::size_t attempts = 0;
    VirtualDualSenseStartupFailure failure = VirtualDualSenseStartupFailure::None;
    std::chrono::steady_clock::time_point inputReadyAt{};
    std::chrono::steady_clock::time_point verifiedAt{};
};

using VirtualDualSenseFirmwareProbe =
    std::function<std::optional<VirtualDualSenseReadiness>(std::string& error)>;
using VirtualDualSenseRemovalWait = std::function<bool(std::string& error)>;

struct VirtualDualSenseStartupHooks {
    std::function<void(std::size_t attempt)> beforeAttempt;
    std::function<bool()> stopRequested;
};

// Opens and seeds a virtual DualSense, then requires the independently
// enumerated Windows HID firmware interface to become readable. A readiness
// failure is retried only after the first device has been fully closed and its
// HID interface has disappeared. Every failure leaves the backend closed.
bool startVerifiedVirtualDualSense(
    VirtualDualSense& backend,
    const DualSenseInputState& initialInput,
    VirtualDualSense::FeedbackHandler feedbackHandler,
    const VirtualDualSenseFirmwareProbe& probeFirmware,
    const VirtualDualSenseRemovalWait& waitForRemoval,
    std::size_t maximumAttempts,
    VirtualDualSenseStartupResult& result,
    std::string& error);

bool startVerifiedVirtualDualSense(
    VirtualDualSense& backend,
    const DualSenseInputState& initialInput,
    VirtualDualSense::FeedbackHandler feedbackHandler,
    const VirtualDualSenseFirmwareProbe& probeFirmware,
    const VirtualDualSenseRemovalWait& waitForRemoval,
    std::size_t maximumAttempts,
    VirtualDualSenseStartupResult& result,
    std::string& error,
    const VirtualDualSenseStartupHooks& hooks);

} // namespace asb::dualsense
