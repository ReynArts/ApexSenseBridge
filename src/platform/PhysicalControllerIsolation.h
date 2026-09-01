#pragma once

#include "core/DeviceInfo.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

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
                  std::string& error);
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

} // namespace asb::platform
