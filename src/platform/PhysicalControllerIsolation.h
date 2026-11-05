#pragma once

#include "core/DeviceInfo.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asb::flydigi {
class Apex5Device;
}

namespace asb::platform {

struct ControllerRecoveryReport {
    bool recovered = false;
    bool visibilityRestored = true;
    std::string notice;
};

// Temporarily hides only the selected APEX game-controller interfaces from
// other processes. The bridge remains allowed to read them and restores the
// complete HidHide configuration when it exits.
class TemporaryPhysicalControllerIsolation {
public:
    TemporaryPhysicalControllerIsolation();
    ~TemporaryPhysicalControllerIsolation();

    TemporaryPhysicalControllerIsolation(const TemporaryPhysicalControllerIsolation&) = delete;
    TemporaryPhysicalControllerIsolation& operator=(const TemporaryPhysicalControllerIsolation&) = delete;

    bool activate(const HidDeviceInfo& apexInterface,
                  std::string_view sessionToken,
                  std::optional<std::uint8_t> originalApexProfile,
                  std::string& error,
                  const std::function<bool()>& stopRequested = {});
    bool armApexInputTransportRestore(bool originalControllerData,
                                      bool originalRawData,
                                      std::string& error) noexcept;
    bool confirmApexProfileRestored(std::string& error) noexcept;
    bool restore(std::string& error) noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool recoveredStaleIsolation() const noexcept;

    // Internal recovery entry points used by the same executable from RunOnce
    // and from the crash watchdog.
    // New sessions recover visibility first and defer controller writes so a
    // saved profile cannot override their Keep current / explicit slot choice.
    static bool recoverPending(ControllerRecoveryReport& report,
                               std::string& error,
                               bool startingSession = false) noexcept;
    static bool applyDeferredApexRestore(flydigi::Apex5Device& device,
                                         std::string& notice,
                                         std::string& error,
                                         bool keepCurrentProfile = false) noexcept;
    static int watchAndRecover(std::uint32_t ownerProcessId,
                               std::string_view sessionToken,
                               std::string& error) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

namespace detail {

// Kept separate from registry enumeration so the third-party application
// identity guard can be unit-tested without changing HidHide state.
[[nodiscard]] bool matchesFlydigiSpaceStationInstall(
    std::wstring_view displayName,
    std::wstring_view publisher) noexcept;

// Requires the complete, vendor-specific PnP topology. Only gamepad HID
// collections rooted in Space Station's verified GeniTech bus match, so its
// DualSense and XInput proxies can be hidden without selecting physical or
// VIIPER devices by VID/PID alone.
[[nodiscard]] bool matchesFlydigiVirtualGamepadTopology(
    std::wstring_view hidInstanceId,
    std::wstring_view parentInstanceId,
    std::wstring_view rootInstanceId,
    std::wstring_view rootService) noexcept;

[[nodiscard]] bool matchesFlydigiVirtualGamepadRoot(
    std::wstring_view rootInstanceId,
    std::wstring_view rootService) noexcept;

[[nodiscard]] bool matchesApexProfileRecoveryDevice(
    const HidDeviceInfo& candidate,
    std::wstring_view originalPath,
    std::wstring_view originalContainerId,
    std::uint16_t vendorId,
    std::uint16_t productId,
    std::uint16_t usagePage) noexcept;

struct ApexSettingsRestore {
    std::wstring vendorPath;
    std::wstring containerId;
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    std::uint16_t usagePage = 0;
    bool profilePending = false;
    std::uint8_t profileSlot = 0;
    bool inputTransportPending = false;
    bool controllerData = true;
    bool rawData = false;
};

enum class ApexRestoreTarget { Present, Absent, Unknown };

enum class ApexRestoreOutcome { Restored, Absent, Unresponsive, Failed, NotAttempted };

enum class ApexRecoveryMode { OwnSession, StaleSession, SessionActivation };

inline constexpr std::wstring_view kRecoveryRegistryKey =
    L"Software\\ApexSenseBridge\\PhysicalControllerIsolation";
inline constexpr std::wstring_view kDeferredApexRestoreRegistryKey =
    L"Software\\ApexSenseBridge\\DeferredApexRestore";

[[nodiscard]] ApexRestoreTarget locateApexRestoreTarget(
    const ApexSettingsRestore& settings,
    const std::vector<HidDeviceInfo>& candidates,
    bool enumerationFailed) noexcept;

[[nodiscard]] bool defersApexRestore(ApexRecoveryMode mode,
                                     ApexRestoreOutcome outcome) noexcept;

[[nodiscard]] std::optional<ApexSettingsRestore> remainingDeferredApexRestore(
    const ApexSettingsRestore& deferred,
    bool profileRestored,
    bool inputTransportRestored);

[[nodiscard]] bool recoveryMarkerUnchanged(
    std::optional<std::uint32_t> currentOwnerProcessId,
    std::uint32_t expectedOwnerProcessId) noexcept;

[[nodiscard]] bool mergeDeferredApexRestore(
    std::optional<ApexSettingsRestore>& deferred,
    const ApexSettingsRestore& pending);

[[nodiscard]] bool deferredApexRestoreBlocksSession(
    const ApexSettingsRestore& deferred,
    const HidDeviceInfo& sessionInterface) noexcept;

// HidHide's control device accepts one client at a time, so another
// application that configures HidHide (DSX, DS4Windows, the HidHide
// Configuration Client...) makes every open fail with ERROR_ACCESS_DENIED.
// Returns the canonical names of such known applications found among the
// running executable names, each once, in the known-application order.
[[nodiscard]] std::vector<std::string> findHidHideControlClients(
    const std::vector<std::wstring>& runningExecutables);

// Actionable message for a busy (access-denied or sharing-violation) open of
// the HidHide control device. Always starts with kHidHideBusyMarker.
[[nodiscard]] std::string describeHidHideControlDenied(
    const std::vector<std::string>& runningClients);

inline constexpr std::string_view kHidHideBusyMarker = "HidHide is busy: ";

// Another application (DSX at Windows startup, ...) often releases the control
// device within seconds, so activation waits this long before giving up.
inline constexpr std::chrono::milliseconds kHidHideActivationOpenWindow{10000};
inline constexpr std::chrono::milliseconds kHidHideRecoveryOpenWindow{400};

// Backoff before the next open attempt (100 ms doubling to 500 ms, clamped to
// the remaining window); zero once the window is spent.
[[nodiscard]] std::chrono::milliseconds nextHidHideOpenDelay(
    int failedAttempts,
    std::chrono::milliseconds elapsed,
    std::chrono::milliseconds window) noexcept;

} // namespace detail

} // namespace asb::platform
