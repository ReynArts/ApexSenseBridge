#ifdef __linux__

#include "platform/PhysicalControllerIsolation.h"

#include "flydigi/Apex5Device.h"
#include "platform/linux/LinuxPaths.h"
#include "platform/linux/LinuxText.h"

#include <fcntl.h>
#include <libudev.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace asb::platform {
namespace {

namespace fs = std::filesystem;

struct UdevDeleter {
    void operator()(struct udev* handle) const noexcept { udev_unref(handle); }
};
struct UdevEnumerateDeleter {
    void operator()(struct udev_enumerate* handle) const noexcept {
        udev_enumerate_unref(handle);
    }
};
struct UdevDeviceDeleter {
    void operator()(struct udev_device* handle) const noexcept {
        udev_device_unref(handle);
    }
};

std::string_view safeString(const char* value) noexcept {
    return value ? std::string_view(value) : std::string_view{};
}

// Desktop daemons that claim the same pad and republish it as a second virtual
// controller. padctl is the Linux counterpart of Flydigi Space Station on
// Windows: it targets the APEX by udev rule, holds its vendor interface open,
// and -- because it also targets 054C:0CE6 -- would attach to the very
// DualSense this bridge creates, turning one pad into three. Suspending it for
// the session is the Linux equivalent of the Space Station proxy isolation.
constexpr std::string_view kConflictingUserUnits[] = {"padctl.service"};

// Runs systemctl --user and reports whether it succeeded. Never inherits the
// caller's stdout so daemon chatter cannot corrupt the CLI's own output.
bool runUserSystemctl(std::string_view verb, std::string_view unit) noexcept {
    const pid_t child = ::fork();
    if (child < 0) {
        return false;
    }
    if (child == 0) {
        const int devnull = ::open("/dev/null", O_RDWR | O_CLOEXEC);
        if (devnull >= 0) {
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
        }
        const std::string verbArg(verb);
        const std::string unitArg(unit);
        ::execlp("systemctl", "systemctl", "--user", verbArg.c_str(),
                 unitArg.c_str(), nullptr);
        ::_exit(127);
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool userUnitIsActive(std::string_view unit) noexcept {
    return runUserSystemctl("is-active", unit);
}

fs::path markerPath() {
    return linux_paths::stateDirectory() / "pending-restore";
}

fs::path sessionEnvPath() {
    return linux_paths::runtimeDirectory() / "session.env";
}

// A deliberately small key=value store. The marker is internal state read only
// by this program's own recovery path, so it needs no parser beyond this.
std::map<std::string, std::string> readMarker() {
    std::map<std::string, std::string> values;
    std::ifstream file(markerPath());
    std::string line;
    while (std::getline(file, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        values[line.substr(0, separator)] = line.substr(separator + 1);
    }
    return values;
}

bool writeMarker(const std::map<std::string, std::string>& values, std::string& error) {
    if (!linux_paths::ensureDirectory(linux_paths::stateDirectory(), error)) {
        return false;
    }
    const auto target = markerPath();
    const auto temporary = fs::path(target).concat(".tmp");
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file) {
            error = "Could not write " + temporary.string();
            return false;
        }
        for (const auto& [key, value] : values) {
            file << key << '=' << value << '\n';
        }
        if (!file) {
            error = "Could not write " + temporary.string();
            return false;
        }
    }
    std::error_code code;
    fs::rename(temporary, target, code);
    if (code) {
        error = "Could not replace " + target.string() + ": " + code.message();
        return false;
    }
    return true;
}

void removeMarker() noexcept {
    std::error_code ignored;
    fs::remove(markerPath(), ignored);
}

bool processAlive(std::uint32_t pid) noexcept {
    if (pid == 0) {
        return false;
    }
    std::error_code ignored;
    return fs::exists("/proc/" + std::to_string(pid), ignored);
}

// Every evdev joystick node that belongs to the same physical device as the
// APEX vendor interface. These are what SDL, Wine and Steam Input enumerate,
// so these are what a bridge session must take away from them.
std::vector<std::string> joystickNodesForContainer(const std::wstring& containerId) {
    std::vector<std::string> nodes;
    if (containerId.empty()) {
        return nodes;
    }
    const auto container = linux_text::narrow(containerId);

    const std::unique_ptr<struct udev, UdevDeleter> context(udev_new());
    if (!context) {
        return nodes;
    }
    const std::unique_ptr<struct udev_enumerate, UdevEnumerateDeleter> enumerate(
        udev_enumerate_new(context.get()));
    if (!enumerate) {
        return nodes;
    }
    udev_enumerate_add_match_subsystem(enumerate.get(), "input");
    udev_enumerate_scan_devices(enumerate.get());

    struct udev_list_entry* entry = nullptr;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate.get())) {
        const char* syspath = udev_list_entry_get_name(entry);
        if (!syspath) {
            continue;
        }
        const std::unique_ptr<struct udev_device, UdevDeviceDeleter> node(
            udev_device_new_from_syspath(context.get(), syspath));
        if (!node) {
            continue;
        }
        const auto devnode = safeString(udev_device_get_devnode(node.get()));
        if (devnode.rfind("/dev/input/event", 0) != 0) {
            continue;
        }
        if (safeString(udev_device_get_property_value(node.get(), "ID_INPUT_JOYSTICK")) != "1") {
            continue;
        }
        struct udev_device* usbDevice = udev_device_get_parent_with_subsystem_devtype(
            node.get(), "usb", "usb_device");
        if (!usbDevice) {
            continue;
        }
        if (safeString(udev_device_get_syspath(usbDevice)) != container) {
            continue;
        }
        nodes.emplace_back(devnode);
    }
    return nodes;
}

} // namespace

struct TemporaryPhysicalControllerIsolation::Impl {
    std::vector<int> grabbed;
    std::vector<std::string> suspendedUnits;
    bool active = false;
    bool recoveredStale = false;
    std::wstring containerId;
    std::string sessionToken;

    // Bringing a suspended daemon back must never fail the caller: the session
    // is already over by then, and a daemon left down is a nuisance, not a
    // reason to report failure.
    void restoreSuspendedUnits() noexcept {
        for (const auto& unit : suspendedUnits) {
            runUserSystemctl("start", unit);
        }
        suspendedUnits.clear();
    }

    void releaseGrabs() noexcept {
        for (const int descriptor : grabbed) {
            // Closing the descriptor releases the grab even if the ioctl fails,
            // which is what makes this crash-safe in the first place.
            ioctl(descriptor, EVIOCGRAB, 0);
            ::close(descriptor);
        }
        grabbed.clear();
    }
};

TemporaryPhysicalControllerIsolation::TemporaryPhysicalControllerIsolation()
    : impl_(std::make_unique<Impl>()) {}

TemporaryPhysicalControllerIsolation::~TemporaryPhysicalControllerIsolation() {
    if (impl_) {
        impl_->releaseGrabs();
        // Daemons are suspended before the identity exchange, which is well
        // before activate(), so any early return between the two would otherwise
        // leave the user without their controller daemon.
        impl_->restoreSuspendedUnits();
    }
}

bool TemporaryPhysicalControllerIsolation::suspendConflictingDaemons(
    std::string& error) noexcept {
    if (!impl_) {
        error = "Controller isolation is not available.";
        return false;
    }
    bool suspendedAny = false;
    for (const auto unit : kConflictingUserUnits) {
        if (!userUnitIsActive(unit)) {
            continue;
        }
        if (!runUserSystemctl("stop", unit)) {
            error = "Could not suspend " + std::string(unit) +
                    ", which holds the controller and republishes it as a second "
                    "virtual pad. Stop it manually with 'systemctl --user stop " +
                    std::string(unit) + "' and retry.";
            impl_->restoreSuspendedUnits();
            return false;
        }
        impl_->suspendedUnits.emplace_back(unit);
        suspendedAny = true;
    }
    // Give udev and the daemon a moment to let go of the nodes. Only worth
    // waiting when something was actually stopped by this call.
    if (suspendedAny) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    error.clear();
    return true;
}

bool TemporaryPhysicalControllerIsolation::activate(
    const HidDeviceInfo& apexInterface,
    std::string_view sessionToken,
    std::optional<std::uint8_t> originalApexProfile,
    std::string& error) {
    if (impl_->active) {
        error = "Physical controller isolation is already active.";
        return false;
    }

    // A marker left by a crashed session is cleared before this one arms its
    // own, mirroring the Windows stale-isolation recovery.
    bool recovered = false;
    std::string recoveryError;
    if (recoverPending(recovered, recoveryError)) {
        impl_->recoveredStale = recovered;
    }

    // Normally already done before the identity exchange; harmless if repeated.
    if (!suspendConflictingDaemons(error)) {
        return false;
    }

    const auto nodes = joystickNodesForContainer(apexInterface.containerId);
    for (const auto& node : nodes) {
        const int descriptor = ::open(node.c_str(), O_RDONLY | O_CLOEXEC);
        if (descriptor < 0) {
            const int code = errno;
            impl_->releaseGrabs();
            impl_->restoreSuspendedUnits();
            error = "Could not open " + node + " to isolate the physical APEX (errno " +
                    std::to_string(code) + ": " + std::strerror(code) + ").";
            return false;
        }
        if (ioctl(descriptor, EVIOCGRAB, 1) < 0) {
            const int code = errno;
            ::close(descriptor);
            impl_->releaseGrabs();
            impl_->restoreSuspendedUnits();
            error = "Another process already holds " + node +
                    " exclusively (errno " + std::to_string(code) + ": " +
                    std::strerror(code) +
                    "). Close Steam Input or any other controller daemon and retry.";
            return false;
        }
        impl_->grabbed.push_back(descriptor);
    }

    // SDL still enumerates a grabbed device, so a game would see a phantom
    // second pad. Launchers source this file to suppress it.
    // SDL expects four hex digits per id, so the width has to be forced.
    std::ostringstream ignore;
    ignore << "0x" << std::hex << std::setfill('0') << std::setw(4)
           << apexInterface.vendorId << "/0x" << std::setw(4)
           << apexInterface.productId;
    std::string environmentError;
    if (linux_paths::ensureDirectory(linux_paths::runtimeDirectory(), environmentError)) {
        std::ofstream environment(sessionEnvPath(), std::ios::trunc);
        environment << "SDL_GAMECONTROLLER_IGNORE_DEVICES=" << ignore.str() << '\n'
                    << "SDL_JOYSTICK_IGNORE_DEVICES=" << ignore.str() << '\n';
    }

    std::map<std::string, std::string> marker{
        {"owner_pid", std::to_string(::getpid())},
        {"session_token", std::string(sessionToken)},
        {"container", linux_text::narrow(apexInterface.containerId)},
    };
    if (originalApexProfile) {
        marker["apex_profile"] = std::to_string(*originalApexProfile);
    }
    if (!impl_->suspendedUnits.empty()) {
        std::string units;
        for (const auto& unit : impl_->suspendedUnits) {
            if (!units.empty()) units.push_back(',');
            units += unit;
        }
        marker["suspended_units"] = units;
    }
    // Armed before the controller receives any temporary switch command, so a
    // crash between the two can never lose the original profile.
    if (!writeMarker(marker, error)) {
        impl_->releaseGrabs();
        impl_->restoreSuspendedUnits();
        return false;
    }

    impl_->containerId = apexInterface.containerId;
    impl_->sessionToken = std::string(sessionToken);
    impl_->active = true;
    return true;
}

bool TemporaryPhysicalControllerIsolation::armApexInputTransportRestore(
    bool originalControllerData, bool originalRawData, std::string& error) noexcept {
    if (!impl_->active) {
        error = "Cannot arm input-transport recovery without an active session.";
        return false;
    }
    auto marker = readMarker();
    marker["input_transport_controller"] = originalControllerData ? "1" : "0";
    marker["input_transport_raw"] = originalRawData ? "1" : "0";
    return writeMarker(marker, error);
}

bool TemporaryPhysicalControllerIsolation::confirmApexProfileRestored(
    std::string& error) noexcept {
    if (!impl_->active) {
        return true;
    }
    auto marker = readMarker();
    marker.erase("apex_profile");
    return writeMarker(marker, error);
}

bool TemporaryPhysicalControllerIsolation::restore(std::string& error) noexcept {
    error.clear();
    if (!impl_->active) {
        return true;
    }
    impl_->releaseGrabs();
    impl_->restoreSuspendedUnits();
    std::error_code ignored;
    fs::remove(sessionEnvPath(), ignored);
    removeMarker();
    impl_->active = false;
    return true;
}

bool TemporaryPhysicalControllerIsolation::active() const noexcept {
    return impl_->active;
}

bool TemporaryPhysicalControllerIsolation::recoveredStaleIsolation() const noexcept {
    return impl_->recoveredStale;
}

bool TemporaryPhysicalControllerIsolation::recoverPending(
    bool& recovered, std::string& error) noexcept {
    recovered = false;
    error.clear();

    const auto marker = readMarker();
    if (marker.empty()) {
        return true;
    }

    const auto owner = marker.find("owner_pid");
    if (owner != marker.end()) {
        const auto pid = static_cast<std::uint32_t>(
            std::strtoul(owner->second.c_str(), nullptr, 10));
        if (processAlive(pid) && pid != static_cast<std::uint32_t>(::getpid())) {
            // A live owner is mid-session; its own restore path owns this.
            return true;
        }
    }

    // Suspended daemons do outlive a crash, so they are restarted first: unlike
    // the grabs, nothing in the kernel brings them back on its own.
    const auto suspended = marker.find("suspended_units");
    if (suspended != marker.end()) {
        std::string_view remaining = suspended->second;
        while (!remaining.empty()) {
            const auto separator = remaining.find(',');
            const auto unit = remaining.substr(0, separator);
            if (!unit.empty()) {
                runUserSystemctl("start", unit);
                recovered = true;
            }
            if (separator == std::string_view::npos) {
                break;
            }
            remaining.remove_prefix(separator + 1);
        }
    }

    // Visibility needs no recovery: the kernel dropped every EVIOCGRAB when the
    // owning process died. Only the onboard profile can outlive a crash.
    const auto profile = marker.find("apex_profile");
    if (profile != marker.end()) {
        const auto slot = static_cast<std::uint8_t>(
            std::strtoul(profile->second.c_str(), nullptr, 10));
        std::string candidateError;
        const auto candidates = flydigi::Apex5Device::findCandidates(candidateError);
        bool restored = false;
        for (const auto& candidate : candidates) {
            std::string openError;
            auto device = flydigi::Apex5Device::open(candidate, openError);
            if (!device) {
                continue;
            }
            std::string applyError;
            if (device->applyProfile(slot, applyError)) {
                restored = true;
                break;
            }
            error = applyError;
        }
        if (!restored) {
            if (error.empty()) {
                error = "No APEX controller was available to restore onboard profile " +
                        std::to_string(slot + 1) + ".";
            }
            return false;
        }
        recovered = true;
    }

    std::error_code ignored;
    fs::remove(sessionEnvPath(), ignored);
    removeMarker();
    return true;
}

int TemporaryPhysicalControllerIsolation::watchAndRecover(
    std::uint32_t ownerProcessId,
    std::string_view sessionToken,
    std::string& error) noexcept {
    (void)sessionToken;

    const int pidDescriptor = static_cast<int>(
        ::syscall(SYS_pidfd_open, static_cast<pid_t>(ownerProcessId), 0u));
    if (pidDescriptor >= 0) {
        struct pollfd waiter {};
        waiter.fd = pidDescriptor;
        waiter.events = POLLIN;
        while (::poll(&waiter, 1, -1) < 0) {
            if (errno != EINTR) {
                break;
            }
        }
        ::close(pidDescriptor);
    } else if (errno != ESRCH) {
        error = "Could not watch the owning process (errno " +
                std::to_string(errno) + ": " + std::strerror(errno) + ").";
        return 1;
    }

    bool recovered = false;
    return recoverPending(recovered, error) ? 0 : 1;
}

} // namespace asb::platform

#endif // __linux__
