#include "platform/PhysicalControllerIsolation.h"

namespace asb::platform {

struct TemporaryPhysicalControllerIsolation::Impl {};

TemporaryPhysicalControllerIsolation::TemporaryPhysicalControllerIsolation()
    : impl_(std::make_unique<Impl>()) {}
TemporaryPhysicalControllerIsolation::~TemporaryPhysicalControllerIsolation() = default;

bool TemporaryPhysicalControllerIsolation::activate(
    const HidDeviceInfo&, std::string_view, std::string& error) {
    error = "Physical controller isolation is only available on Windows.";
    return false;
}

bool TemporaryPhysicalControllerIsolation::restore(std::string&) noexcept { return true; }
bool TemporaryPhysicalControllerIsolation::active() const noexcept { return false; }
bool TemporaryPhysicalControllerIsolation::recoveredStaleIsolation() const noexcept { return false; }

bool TemporaryPhysicalControllerIsolation::recoverPending(
    bool& recovered, std::string&) noexcept {
    recovered = false;
    return true;
}

int TemporaryPhysicalControllerIsolation::watchAndRecover(
    std::uint32_t, std::string_view, std::string& error) noexcept {
    error = "The HidHide recovery watchdog is only available on Windows.";
    return 1;
}

} // namespace asb::platform
