#include "platform/PhysicalControllerIsolation.h"

namespace asb::platform {

struct TemporaryPhysicalControllerIsolation::Impl {};

TemporaryPhysicalControllerIsolation::TemporaryPhysicalControllerIsolation()
    : impl_(std::make_unique<Impl>()) {}
TemporaryPhysicalControllerIsolation::~TemporaryPhysicalControllerIsolation() = default;

bool TemporaryPhysicalControllerIsolation::activate(
    const HidDeviceInfo&, std::string_view, std::optional<std::uint8_t>,
    std::string& error, const std::function<bool()>&) {
    error = "Physical controller isolation is only available on Windows.";
    return false;
}

bool TemporaryPhysicalControllerIsolation::armApexInputTransportRestore(
    bool, bool, std::string& error) noexcept {
    error = "Apex input-transport recovery is only available on Windows.";
    return false;
}

bool TemporaryPhysicalControllerIsolation::confirmApexProfileRestored(
    std::string&) noexcept { return true; }
bool TemporaryPhysicalControllerIsolation::refreshTargets(
    const HidDeviceInfo&, std::string&) noexcept { return true; }
bool TemporaryPhysicalControllerIsolation::restore(std::string&, bool) noexcept { return true; }
bool TemporaryPhysicalControllerIsolation::active() const noexcept { return false; }
bool TemporaryPhysicalControllerIsolation::recoveredStaleIsolation() const noexcept { return false; }

bool TemporaryPhysicalControllerIsolation::recoverPending(
    ControllerRecoveryReport& report, std::string&) noexcept {
    report = ControllerRecoveryReport{};
    return true;
}

bool TemporaryPhysicalControllerIsolation::applyDeferredApexRestore(
    flydigi::Apex5Device&, std::string&, std::string&) noexcept {
    return true;
}

int TemporaryPhysicalControllerIsolation::watchAndRecover(
    std::uint32_t, std::string_view, std::string& error) noexcept {
    error = "The HidHide recovery watchdog is only available on Windows.";
    return 1;
}

} // namespace asb::platform
