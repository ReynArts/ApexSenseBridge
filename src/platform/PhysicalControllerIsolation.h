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

namespace asb::platform {

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
    static bool recoverPending(bool& recovered, std::string& error) noexcept;
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
