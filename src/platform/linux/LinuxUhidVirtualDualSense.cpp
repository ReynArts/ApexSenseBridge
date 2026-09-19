#include "dualsense/DualSenseDescriptor.h"
#include "dualsense/DualSenseFeatureReports.h"
#include "dualsense/DualSenseFeedback.h"
#include "dualsense/DualSenseInput.h"
#include "dualsense/VirtualDualSense.h"
#include "platform/linux/LinuxVirtualDualSenseBackends.h"

#include <fcntl.h>
#include <linux/uhid.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <span>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace asb::dualsense {
namespace {

constexpr std::uint16_t kSonyVendorId = 0x054C;
constexpr std::uint16_t kDualSenseProductId = 0x0CE6;
constexpr std::uint16_t kDualSenseVersion = 0x0100;
constexpr auto kStartTimeout = std::chrono::milliseconds(2000);

// The DualSense sensor timestamp advances in 0.33 us units.
constexpr std::uint32_t kSensorTicksPerMicrosecond = 3;

// A wired DualSense reports on a 4 ms interrupt interval, which is what the
// report descriptor this backend publishes declares. uhid has no endpoint to
// enforce that, so the cadence has to be enforced here: an APEX streams at
// roughly 1 kHz, and forwarding every one of those reports floods a consumer
// written for 250 Hz until it is reading state from milliseconds ago.
constexpr auto kReportInterval = std::chrono::microseconds(4000);

std::string errnoMessage(const char* what, int code) {
    return std::string(what) + " failed (errno " + std::to_string(code) + ": " +
           std::strerror(code) + ")";
}

std::string kernelRelease() {
    struct utsname info {};
    if (uname(&info) != 0) {
        return "unknown";
    }
    return info.release;
}

class UhidVirtualDualSense final : public VirtualDualSense {
public:
    explicit UhidVirtualDualSense(VirtualDualSenseOptions options)
        : options_(std::move(options)) {}

    ~UhidVirtualDualSense() override { close(); }

    bool open(std::string& error, FeedbackHandler handler) override {
        close();
        resetStats();

        if (options_.backend == VirtualDualSenseBackend::Sidecar) {
            error = "The VIIPER sidecar backend is not available on Linux. The "
                    "integrated uhid backend is the only virtual DualSense here.";
            return false;
        }

        const auto startedAt = std::chrono::steady_clock::now();
        dumpStartedAt_ = startedAt;
        if (const char* dump = std::getenv("ASB_DUMP_EFFECTS"); dump && *dump) {
            dumpPath_ = dump;
        }
        handler_ = std::move(handler);

        fd_ = ::open("/dev/uhid", O_RDWR | O_CLOEXEC);
        if (fd_ < 0) {
            const int code = errno;
            if (code == ENOENT) {
                error = "/dev/uhid is missing. Load the kernel module with "
                        "'modprobe uhid'.";
            } else if (code == EACCES || code == EPERM) {
                error = "/dev/uhid cannot be opened. Add the user to the 'input' "
                        "group, or install the ApexSenseBridge udev rule.";
            } else {
                error = errnoMessage("open(/dev/uhid)", code);
            }
            handler_ = {};
            return false;
        }

        wakeFd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (wakeFd_ < 0) {
            error = errnoMessage("eventfd", errno);
            closeDescriptors();
            handler_ = {};
            return false;
        }

        if (!sendCreate(error)) {
            closeDescriptors();
            handler_ = {};
            return false;
        }
        const auto createdAt = std::chrono::steady_clock::now();

        running_.store(true, std::memory_order_release);
        eventThread_ = std::thread([this] { runEventLoop(); });

        if (!waitForStart(error)) {
            close();
            return false;
        }
        const auto startedReportingAt = std::chrono::steady_clock::now();

        // A real controller reports as soon as it enumerates. Seeding one
        // neutral report keeps readers from seeing an idle, state-less device.
        std::string inputError;
        if (!updateInput(DualSenseInputState{}, inputError)) {
            error = "The virtual DualSense was created but the first input "
                    "report failed: " + inputError;
            close();
            return false;
        }

        const auto readyAt = std::chrono::steady_clock::now();
        const auto microseconds = [](auto from, auto to) {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(to - from).count());
        };
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.initializationBootstrapUs = microseconds(startedAt, createdAt);
        stats_.initializationDeviceUs = microseconds(createdAt, startedReportingAt);
        stats_.initializationFeedbackUs = microseconds(startedReportingAt, readyAt);
        stats_.initializationInputUs = microseconds(startedReportingAt, readyAt);
        return true;
    }

    void close() noexcept override {
        if (fd_ < 0 && !eventThread_.joinable()) {
            handler_ = {};
            return;
        }

        running_.store(false, std::memory_order_release);
        wakeEventLoop();
        if (eventThread_.joinable()) {
            eventThread_.join();
        }

        if (fd_ >= 0 && created_) {
            struct uhid_event event {};
            event.type = UHID_DESTROY;
            std::string ignored;
            writeEvent(event, ignored);
        }
        created_ = false;
        started_.store(false, std::memory_order_release);
        closeDescriptors();

        // Joined above, so no feedback callback can be in flight here.
        handler_ = {};
    }

    bool updateInput(const DualSenseInputState& state, std::string& error) override {
        if (fd_ < 0 || !created_) {
            error = "The virtual DualSense is not open.";
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(counterMutex_);
            if (lastReportAt_.time_since_epoch().count() != 0 &&
                now - lastReportAt_ < kReportInterval) {
                // Too soon for the declared cadence. Dropping is safe and is
                // what keeps this path free of any other thread: the physical
                // source runs at roughly 1 kHz, so a newer state is a
                // millisecond away, and when input does stop the bridge's own
                // timeout and keepalive paths call back in with the last state.
                return true;
            }
        }
        return emitReport(state, now, error);
    }

    [[nodiscard]] bool connected() const noexcept override {
        return created_ && started_.load(std::memory_order_acquire);
    }

    VirtualDualSenseStats stats() const override {
        std::lock_guard<std::mutex> lock(statsMutex_);
        VirtualDualSenseStats snapshot = stats_;
        snapshot.connected = connected();
        return snapshot;
    }

private:
    bool emitReport(const DualSenseInputState& state,
                    std::chrono::steady_clock::time_point now,
                    std::string& error) {
        struct uhid_event event {};
        event.type = UHID_INPUT2;
        {
            std::lock_guard<std::mutex> lock(counterMutex_);
            if (lastReportAt_.time_since_epoch().count() != 0) {
                const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                    now - lastReportAt_).count();
                counters_.advance(static_cast<std::uint32_t>(
                    elapsedUs * kSensorTicksPerMicrosecond));
            }
            lastReportAt_ = now;
            const auto report = buildDualSenseUsbInputReport(state, counters_);
            event.u.input2.size = static_cast<std::uint16_t>(report.size());
            std::copy(report.begin(), report.end(), event.u.input2.data);
        }
        if (!writeEvent(event, error)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(statsMutex_);
        ++stats_.inputUpdates;
        return true;
    }

    void resetStats() {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_ = VirtualDualSenseStats{};
        stats_.backendVersion = "uhid (kernel " + kernelRelease() + ")";
        std::lock_guard<std::mutex> counterLock(counterMutex_);
        counters_ = DualSenseUsbReportCounters{};
        lastReportAt_ = {};
    }

    void closeDescriptors() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        if (wakeFd_ >= 0) {
            ::close(wakeFd_);
            wakeFd_ = -1;
        }
    }

    bool writeEvent(const struct uhid_event& event, std::string& error) {
        std::lock_guard<std::mutex> lock(writeMutex_);
        if (fd_ < 0) {
            error = "The virtual DualSense device is closed.";
            return false;
        }
        const ssize_t written = ::write(fd_, &event, sizeof(event));
        if (written < 0) {
            error = errnoMessage("write(/dev/uhid)", errno);
            return false;
        }
        if (static_cast<std::size_t>(written) != sizeof(event)) {
            error = "Short write to /dev/uhid.";
            return false;
        }
        return true;
    }

    bool sendCreate(std::string& error) {
        const auto descriptor = usbReportDescriptor();
        struct uhid_event event {};
        event.type = UHID_CREATE2;
        std::snprintf(reinterpret_cast<char*>(event.u.create2.name),
                      sizeof(event.u.create2.name), "Wireless Controller");
        std::snprintf(reinterpret_cast<char*>(event.u.create2.phys),
                      sizeof(event.u.create2.phys), "apexsensebridge/uhid");
        std::snprintf(reinterpret_cast<char*>(event.u.create2.uniq),
                      sizeof(event.u.create2.uniq), "c0:13:37:05:02:06");
        std::copy(descriptor.begin(), descriptor.end(), event.u.create2.rd_data);
        event.u.create2.rd_size = static_cast<std::uint16_t>(descriptor.size());
        event.u.create2.bus = BUS_USB;
        event.u.create2.vendor = kSonyVendorId;
        event.u.create2.product = kDualSenseProductId;
        event.u.create2.version = kDualSenseVersion;
        event.u.create2.country = 0;

        if (!writeEvent(event, error)) {
            return false;
        }
        created_ = true;
        return true;
    }

    bool waitForStart(std::string& error) {
        std::unique_lock<std::mutex> lock(startMutex_);
        if (startSignal_.wait_for(lock, kStartTimeout, [this] {
                return started_.load(std::memory_order_acquire) ||
                       !running_.load(std::memory_order_acquire);
            })) {
            if (started_.load(std::memory_order_acquire)) {
                return true;
            }
        }
        error = "The kernel did not bind a driver to the virtual DualSense. "
                "054C:0CE6 is claimed exclusively by hid-playstation, which has "
                "no hid-generic fallback: verify the module is loadable for the "
                "running kernel (modprobe hid_playstation).";
        return false;
    }

    void wakeEventLoop() noexcept {
        if (wakeFd_ < 0) {
            return;
        }
        const std::uint64_t one = 1;
        [[maybe_unused]] const ssize_t ignored = ::write(wakeFd_, &one, sizeof(one));
    }

    void runEventLoop() {
        while (running_.load(std::memory_order_acquire)) {
            struct pollfd fds[2];
            fds[0] = {fd_, POLLIN, 0};
            fds[1] = {wakeFd_, POLLIN, 0};
            const int ready = ::poll(fds, 2, 250);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (ready == 0) {
                continue;
            }
            if ((fds[1].revents & POLLIN) != 0) {
                std::uint64_t drained = 0;
                [[maybe_unused]] const ssize_t ignored =
                    ::read(wakeFd_, &drained, sizeof(drained));
                continue;
            }
            if ((fds[0].revents & POLLIN) == 0) {
                continue;
            }

            struct uhid_event event {};
            const ssize_t bytes = ::read(fd_, &event, sizeof(event));
            if (bytes < 0) {
                if (errno == EINTR || errno == EAGAIN) {
                    continue;
                }
                break;
            }
            if (static_cast<std::size_t>(bytes) < sizeof(event.type)) {
                continue;
            }
            dispatch(event);
        }

        // Unblock open() if the loop died before the device ever started.
        {
            std::lock_guard<std::mutex> lock(startMutex_);
        }
        startSignal_.notify_all();
    }

    void dispatch(const struct uhid_event& event) {
        switch (event.type) {
        case UHID_START: {
            {
                std::lock_guard<std::mutex> lock(startMutex_);
                started_.store(true, std::memory_order_release);
            }
            startSignal_.notify_all();
            break;
        }
        case UHID_STOP:
            started_.store(false, std::memory_order_release);
            break;
        case UHID_OPEN:
        case UHID_CLOSE:
            break;
        case UHID_OUTPUT:
            handleOutput({event.u.output.data, event.u.output.size});
            break;
        case UHID_SET_REPORT: {
            handleOutput({event.u.set_report.data, event.u.set_report.size});
            struct uhid_event reply {};
            reply.type = UHID_SET_REPORT_REPLY;
            reply.u.set_report_reply.id = event.u.set_report.id;
            reply.u.set_report_reply.err = 0;
            std::string ignored;
            writeEvent(reply, ignored);
            break;
        }
        case UHID_GET_REPORT:
            handleGetReport(event.u.get_report);
            break;
        default:
            break;
        }
    }

    void handleOutput(std::span<const std::uint8_t> report) {
        DualSenseFeedback feedback{};
        if (!decodeDualSenseOutputReport(report, feedback)) {
            std::lock_guard<std::mutex> lock(statsMutex_);
            ++stats_.malformedFrames;
            return;
        }
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            appendFeedbackDump(dumpPath_,
                               static_cast<std::uint64_t>(
                                   std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::steady_clock::now() - dumpStartedAt_)
                                       .count()),
                               feedback);
            ++stats_.outputReports;
            if (feedback.hasTriggerEffect()) {
                ++stats_.triggerReports;
            }
            if (feedback.requestsRumbleUpdate() || feedback.hasRumble()) {
                ++stats_.rumbleReports;
            }
        }
        if (handler_) {
            handler_(feedback);
        }
    }

    void handleGetReport(const struct uhid_get_report_req& request) {
        const auto payload = featureReport(request.rnum);
        struct uhid_event reply {};
        reply.type = UHID_GET_REPORT_REPLY;
        reply.u.get_report_reply.id = request.id;
        if (payload.empty()) {
            reply.u.get_report_reply.err = EIO;
        } else {
            reply.u.get_report_reply.err = 0;
            reply.u.get_report_reply.size = static_cast<std::uint16_t>(payload.size());
            std::copy(payload.begin(), payload.end(), reply.u.get_report_reply.data);
        }
        std::string ignored;
        writeEvent(reply, ignored);
    }

    VirtualDualSenseOptions options_;
    FeedbackHandler handler_;

    std::string dumpPath_;
    std::chrono::steady_clock::time_point dumpStartedAt_{};
    int fd_ = -1;
    int wakeFd_ = -1;
    bool created_ = false;
    std::atomic<bool> running_{false};
    std::atomic<bool> started_{false};

    std::thread eventThread_;
    std::mutex writeMutex_;
    std::mutex startMutex_;
    std::condition_variable startSignal_;

    std::mutex counterMutex_;
    DualSenseUsbReportCounters counters_{};
    std::chrono::steady_clock::time_point lastReportAt_{};

    mutable std::mutex statsMutex_;
    VirtualDualSenseStats stats_{};
};

} // namespace

std::unique_ptr<VirtualDualSense> createUhidVirtualDualSense(
    VirtualDualSenseOptions options) {
    return std::make_unique<UhidVirtualDualSense>(std::move(options));
}

} // namespace asb::dualsense
