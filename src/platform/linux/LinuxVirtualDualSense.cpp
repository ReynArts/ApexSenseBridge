#ifdef __linux__

#include "platform/linux/LinuxVirtualDualSenseBackends.h"

#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace asb::dualsense {
namespace {

// libVIIPER presents the pad as a real USB device, which is the only way the
// DualSense haptic audio stream reaches it - uhid creates no audio endpoint. It
// used to be reserved for an explicit --virtual-backend because attaching was
// believed to need root. It does not; it needs the USB/IP client module, write
// access to three paths, and the library itself.
//
// All four are checked, and the library is checked at the path open() will
// actually load rather than by a second guess at the same rule. Leaving it out
// was a real defect: packaging/linux/install.sh grants the three kernel-side
// prerequisites but never installs libVIIPER.so, so every fresh install had
// exactly the combination that chose a backend that could not start, and the
// session failed instead of quietly using the one that always works.
[[nodiscard]] bool libViiperPrerequisitesPresent(
    const VirtualDualSenseOptions& options) noexcept {
    std::error_code ignored;
    if (!std::filesystem::is_regular_file(resolveLibViiperLibraryPath(options),
                                          ignored)) {
        return false;
    }
    if (!std::filesystem::exists("/sys/devices/platform/vhci_hcd.0", ignored)) {
        return false;
    }
    // usbip(8) attaches first and records the connection afterwards, so both
    // have to be writable or the attach is undone right after it succeeds.
    return ::access("/sys/devices/platform/vhci_hcd.0/attach", W_OK) == 0 &&
           ::access("/run/vhci_hcd", W_OK | X_OK) == 0;
}

// Asking for a backend this platform does not have is a mistake worth a
// sentence, not a silent substitution: a session that quietly ran on something
// other than what was asked for is how the selection defect above stayed
// invisible for as long as it did.
class UnavailableVirtualDualSense final : public VirtualDualSense {
public:
    explicit UnavailableVirtualDualSense(std::string reason)
        : reason_(std::move(reason)) {}

    bool open(std::string& error, FeedbackHandler = {}) override {
        error = reason_;
        return false;
    }
    void close() noexcept override {}
    bool updateInput(const DualSenseInputState&, std::string& error) override {
        error = reason_;
        return false;
    }
    [[nodiscard]] bool connected() const noexcept override { return false; }
    VirtualDualSenseStats stats() const override { return {}; }

private:
    std::string reason_;
};

// Auto promises a session that starts. A preflight can only ask whether the
// library is there, not whether it loads, exports what this build calls, or
// manages to attach - and a library that fails any of those took the session
// down with it, on a machine where uhid would have worked. So Auto tries
// libVIIPER and, if opening it fails, says so and continues on uhid. An
// explicit --virtual-backend integrated still fails hard: that asked for one
// backend by name.
class FallbackVirtualDualSense final : public VirtualDualSense {
public:
    explicit FallbackVirtualDualSense(VirtualDualSenseOptions options)
        : options_(std::move(options)) {}

    bool open(std::string& error, FeedbackHandler handler = {}) override {
        active_ = createLibViiperVirtualDualSense(options_);
        std::string libViiperError;
        if (active_ && active_->open(libViiperError, handler)) {
            return true;
        }
        if (active_) {
            active_->close();
        }
        std::cerr << "The libVIIPER backend did not start (" << libViiperError
                  << "); continuing on uhid, which carries everything except "
                     "DualSense audio haptics.\n";
        active_ = createUhidVirtualDualSense(options_);
        if (!active_) {
            error = "no virtual DualSense backend could be created";
            return false;
        }
        return active_->open(error, std::move(handler));
    }

    void close() noexcept override { if (active_) active_->close(); }
    bool updateInput(const DualSenseInputState& state, std::string& error) override {
        if (!active_) { error = "virtual DualSense is not open"; return false; }
        return active_->updateInput(state, error);
    }
    [[nodiscard]] bool connected() const noexcept override {
        return active_ && active_->connected();
    }
    VirtualDualSenseStats stats() const override {
        return active_ ? active_->stats() : VirtualDualSenseStats{};
    }

private:
    VirtualDualSenseOptions options_;
    std::unique_ptr<VirtualDualSense> active_;
};

} // namespace

std::unique_ptr<VirtualDualSense> createVirtualDualSense(VirtualDualSenseOptions options) {
    switch (options.backend) {
    case VirtualDualSenseBackend::Integrated:
        // Explicitly asked for libVIIPER, which is what the Windows build calls
        // its integrated backend too. Honour it even if the checks above would
        // have declined, so its own diagnostics are what the caller sees rather
        // than a guess made here.
        return createLibViiperVirtualDualSense(std::move(options));
    case VirtualDualSenseBackend::Uhid:
        return createUhidVirtualDualSense(std::move(options));
    case VirtualDualSenseBackend::Sidecar:
        return std::make_unique<UnavailableVirtualDualSense>(
            "The sidecar backend runs viiper.exe and exists only on Windows. "
            "Use --virtual-backend auto, integrated or uhid.");
    case VirtualDualSenseBackend::Auto:
    default:
        if (libViiperPrerequisitesPresent(options)) {
            return std::make_unique<FallbackVirtualDualSense>(std::move(options));
        }
        return createUhidVirtualDualSense(std::move(options));
    }
}

} // namespace asb::dualsense

#endif // __linux__
