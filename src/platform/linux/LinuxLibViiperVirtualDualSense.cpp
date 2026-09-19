#ifdef __linux__

#include "platform/linux/LinuxVirtualDualSenseBackends.h"

#include "dualsense/DualSenseFeedback.h"
#include "dualsense/DualSenseInput.h"

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace asb::dualsense {
namespace {

using ViiperHandle = std::uintptr_t;
using ViiperBool = std::uint8_t;

struct USBServerConfig {
    char* address;
    std::uint64_t connectionTimeoutMilliseconds;
    std::uint64_t deviceHandlerConnectTimeoutMilliseconds;
    std::uint32_t writeBatchFlushIntervalMilliseconds;
};

using RawOutputCallback = void (*)(std::uintptr_t, const std::uint8_t*, std::uint32_t);
// libVIIPER's log levels are slog's: DEBUG -4, INFO 0, WARN 4, ERROR 8.
using ViiperLogCallback = void (*)(int level, const char* message);

using NewUSBServerFn = ViiperBool (*)(USBServerConfig*, ViiperHandle*, ViiperLogCallback);
using CloseUSBServerFn = ViiperBool (*)(ViiperHandle);
using CreateUSBBusFn = ViiperBool (*)(ViiperHandle, std::uint32_t*);
using RemoveUSBBusFn = ViiperBool (*)(ViiperHandle, std::uint32_t);
using CreateDualSenseDeviceFn = ViiperBool (*)(ViiperHandle, ViiperHandle*, std::uint32_t,
                                               ViiperBool, std::uint16_t, std::uint16_t,
                                               void*);
using SetDualSenseASBInputStateFn = ViiperBool (*)(ViiperHandle, const std::uint8_t*,
                                                   std::uint32_t);
using SetDualSenseASBRawOutputCallbackFn = ViiperBool (*)(ViiperHandle, RawOutputCallback,
                                                          std::uintptr_t);
using RemoveDualSenseDeviceFn = ViiperBool (*)(ViiperHandle);

// Passing nullptr here makes libVIIPER install slog.DiscardHandler and throw
// every message away, which is how an attach failure reached us as a bare
// "could not attach" with the reason discarded. Forward warnings and errors
// always; DEBUG sits inside the URB loop, so it stays behind an opt-in.
void viiperLog(int level, const char* message) {
    static const bool verbose = [] {
        const char* raw = std::getenv("ASB_VIIPER_LOG");
        return raw && *raw && std::string_view(raw) != "0";
    }();
    if (!verbose && level < 4) {
        return;
    }
    const char* name = level >= 8    ? "ERROR"
                       : level >= 4  ? "WARN "
                       : level >= 0  ? "INFO "
                                     : "DEBUG";
    std::fprintf(stderr, "[viiper %s] %s\n", name, message ? message : "");
    std::fflush(stderr);
}

constexpr auto kAudioWindow = std::chrono::milliseconds(5);

std::filesystem::path executableDirectory() {
    std::error_code code;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", code);
    if (code) {
        return std::filesystem::current_path();
    }
    return self.parent_path();
}

std::string dlErrorMessage() {
    const char* message = dlerror();
    return message ? message : "no detail reported";
}

template <typename Function>
bool resolve(void* library, const char* name, Function& function, std::string& error) {
    dlerror();
    function = reinterpret_cast<Function>(dlsym(library, name));
    if (function) {
        return true;
    }
    error = std::string("libVIIPER.so is missing the required ") + name + " export (" +
            dlErrorMessage() + ").";
    return false;
}

class LibViiperVirtualDualSense final : public VirtualDualSense {
public:
    explicit LibViiperVirtualDualSense(VirtualDualSenseOptions options)
        : options_(std::move(options)) {}

    ~LibViiperVirtualDualSense() override { close(); }

    bool open(std::string& error, FeedbackHandler handler) override {
        dumpStartedAt_ = std::chrono::steady_clock::now();
        if (const char* dump = std::getenv("ASB_DUMP_EFFECTS"); dump && *dump) {
            dumpPath_ = dump;
        }
        close();
        resetStats();
        {
            std::lock_guard handlerLock(handlerMutex_);
            handler_ = std::move(handler);
        }

        const auto elapsedUs = [](const auto startedAt) {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - startedAt).count());
        };

        const auto libraryPath = resolveLibViiperLibraryPath(options_);
        std::error_code exists;
        if (!std::filesystem::is_regular_file(libraryPath, exists)) {
            error = "libVIIPER.so was not found at: " + libraryPath.string();
            clearHandler();
            return false;
        }

        const auto bootstrapStartedAt = std::chrono::steady_clock::now();
        library_ = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!library_) {
            error = "Could not load " + libraryPath.string() + " (" + dlErrorMessage() + ").";
            clearHandler();
            return false;
        }
        if (!resolveFunctions(error)) {
            close();
            return false;
        }
        initializationBootstrapUs_ = elapsedUs(bootstrapStartedAt);

        std::string address = "127.0.0.1:0";
        USBServerConfig config{};
        config.address = address.data();
        config.connectionTimeoutMilliseconds = 30000;
        config.deviceHandlerConnectTimeoutMilliseconds = 5000;
        config.writeBatchFlushIntervalMilliseconds = 1;
        const auto serverStartedAt = std::chrono::steady_clock::now();
        if (!newUSBServerASBLoopback_(&config, &serverHandle_, &viiperLog) || serverHandle_ == 0) {
            error = "libVIIPER could not start its in-process USB server.";
            close();
            return false;
        }
        initializationServerUs_ = elapsedUs(serverStartedAt);

        const auto busStartedAt = std::chrono::steady_clock::now();
        if (!createUSBBus_(serverHandle_, &busId_) || busId_ == 0) {
            error = "libVIIPER could not create a virtual USB bus.";
            close();
            return false;
        }
        initializationBusUs_ = elapsedUs(busStartedAt);

        const auto deviceStartedAt = std::chrono::steady_clock::now();
        if (!createDualSenseDevice_(serverHandle_, &deviceHandle_, busId_, 1, 0, 0, nullptr) ||
            deviceHandle_ == 0) {
            error = "libVIIPER could not attach the virtual DualSense. Attaching runs "
                    "usbip(8), which needs the vhci-hcd module loaded, write access to "
                    "/sys/devices/platform/vhci_hcd.0/attach and detach, and a writable "
                    "/run/vhci_hcd for its connection record - usbip attaches first and "
                    "only then writes that record, so a failure there looks like this "
                    "one. Set ASB_VIIPER_LOG=1 to see what libVIIPER reports. "
                    "packaging/linux/ ships the udev, modules-load and tmpfiles files "
                    "that grant all three to the 'input' group; root is not required.";
            close();
            return false;
        }
        initializationDeviceUs_ = elapsedUs(deviceStartedAt);

        const auto feedbackStartedAt = std::chrono::steady_clock::now();
        callbacksEnabled_.store(true, std::memory_order_release);
        if (!setRawOutputCallback_(deviceHandle_,
                                   &LibViiperVirtualDualSense::rawOutputThunk,
                                   reinterpret_cast<std::uintptr_t>(this))) {
            error = "libVIIPER could not enable DualSense feedback.";
            close();
            return false;
        }
        initializationFeedbackUs_ = elapsedUs(feedbackStartedAt);

        const auto neutral = buildNeutralViiperInput();
        const auto inputStartedAt = std::chrono::steady_clock::now();
        if (!setInputState_(deviceHandle_, neutral.data(),
                            static_cast<std::uint32_t>(neutral.size()))) {
            error = "libVIIPER could not initialize neutral controller input.";
            close();
            return false;
        }
        initializationInputUs_ = elapsedUs(inputStartedAt);

        audioWorkerRunning_ = true;
        try {
            audioWorker_ = std::thread(&LibViiperVirtualDualSense::audioLoop, this);
        } catch (...) {
            error = "Could not start libVIIPER's audio-haptics worker.";
            close();
            return false;
        }

        backendVersion_ = "libVIIPER v0.7.0-asb (vhci-hcd)";
        connected_.store(true, std::memory_order_release);
        error.clear();
        return true;
    }

    void close() noexcept override {
        connected_.store(false, std::memory_order_release);
        callbacksEnabled_.store(false, std::memory_order_release);

        if (deviceHandle_ != 0 && setRawOutputCallback_) {
            (void)setRawOutputCallback_(deviceHandle_, nullptr, 0);
        }
        {
            // Wait out a callback that entered just before it was cleared; the
            // Go frame is copied before this lock is released.
            std::lock_guard callbackLock(callbackMutex_);
        }

        {
            std::lock_guard audioLock(audioMutex_);
            audioWorkerRunning_ = false;
        }
        audioCondition_.notify_all();
        if (audioWorker_.joinable()) {
            audioWorker_.join();
        }

        if (deviceHandle_ != 0 && removeDualSenseDevice_) {
            (void)removeDualSenseDevice_(deviceHandle_);
            deviceHandle_ = 0;
        }
        if (busId_ != 0 && serverHandle_ != 0 && removeUSBBus_) {
            (void)removeUSBBus_(serverHandle_, busId_);
            busId_ = 0;
        }
        if (serverHandle_ != 0 && closeUSBServer_) {
            (void)closeUSBServer_(serverHandle_);
            serverHandle_ = 0;
        }

        // A c-shared Go library owns a process-wide runtime that does not
        // survive being unloaded and reloaded, so the handle is dropped without
        // dlclose. Every USB resource and callback above is still released.
        library_ = nullptr;
        clearFunctions();
        clearHandler();
        pendingAudio_.reset();
    }

    bool updateInput(const DualSenseInputState& state, std::string& error) override {
        if (!connected_.load(std::memory_order_acquire) || deviceHandle_ == 0 ||
            !setInputState_) {
            error = "The virtual DualSense is not connected.";
            return false;
        }
        const auto input = buildViiperInput(state);
        std::lock_guard inputLock(inputMutex_);
        if (!setInputState_(deviceHandle_, input.data(),
                            static_cast<std::uint32_t>(input.size()))) {
            error = "libVIIPER could not update controller input.";
            return false;
        }
        inputUpdates_.fetch_add(1, std::memory_order_relaxed);
        error.clear();
        return true;
    }

    [[nodiscard]] bool connected() const noexcept override {
        return connected_.load(std::memory_order_acquire);
    }

    VirtualDualSenseStats stats() const override {
        VirtualDualSenseStats result{};
        result.connected = connected_.load(std::memory_order_relaxed);
        result.inputUpdates = inputUpdates_.load(std::memory_order_relaxed);
        result.outputReports = outputReports_.load(std::memory_order_relaxed);
        result.triggerReports = triggerReports_.load(std::memory_order_relaxed);
        result.rumbleReports = rumbleReports_.load(std::memory_order_relaxed);
        result.audioHapticsFrames = audioHapticsFrames_.load(std::memory_order_relaxed);
        result.audioHapticsDelivered = audioHapticsDelivered_.load(std::memory_order_relaxed);
        result.audioHapticsCoalesced = audioHapticsCoalesced_.load(std::memory_order_relaxed);
        result.malformedFrames = malformedFrames_.load(std::memory_order_relaxed);
        result.unknownFrames = unknownFrames_.load(std::memory_order_relaxed);
        result.initializationBootstrapUs = initializationBootstrapUs_;
        result.initializationServerUs = initializationServerUs_;
        result.initializationBusUs = initializationBusUs_;
        result.initializationDeviceUs = initializationDeviceUs_;
        result.initializationFeedbackUs = initializationFeedbackUs_;
        result.initializationInputUs = initializationInputUs_;
        result.backendVersion = backendVersion_;
        return result;
    }

private:
    static void rawOutputThunk(std::uintptr_t context, const std::uint8_t* frame,
                               std::uint32_t frameLength) noexcept {
        if (context == 0) return;
        reinterpret_cast<LibViiperVirtualDualSense*>(context)->onRawOutput(frame, frameLength);
    }

    void onRawOutput(const std::uint8_t* frame, std::uint32_t frameLength) noexcept {
        std::lock_guard callbackLock(callbackMutex_);
        if (!callbacksEnabled_.load(std::memory_order_acquire) || !frame || frameLength < 3) {
            return;
        }

        const auto payloadSize = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(frame[1]) |
            (static_cast<std::uint16_t>(frame[2]) << 8));
        if (payloadSize == 0 || frameLength != static_cast<std::uint32_t>(payloadSize) + 3) {
            malformedFrames_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const std::uint8_t frameType = frame[0];
        if (frameType != 0x01 && frameType != 0x02) {
            unknownFrames_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        DualSenseFeedback feedback{};
        if (!decodeViiperFeedbackFrame(
                frameType, std::span<const std::uint8_t>(frame + 3, payloadSize), feedback)) {
            malformedFrames_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        if (feedback.kind == FeedbackKind::HidOutput) {
            appendFeedbackDump(dumpPath_,
                               static_cast<std::uint64_t>(
                                   std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::steady_clock::now() - dumpStartedAt_)
                                       .count()),
                               feedback);
            outputReports_.fetch_add(1, std::memory_order_relaxed);
            if (feedback.hasTriggerEffect()) {
                triggerReports_.fetch_add(1, std::memory_order_relaxed);
            }
            if (feedback.requestsRumbleUpdate()) {
                rumbleReports_.fetch_add(1, std::memory_order_relaxed);
            }
            deliver(feedback);
            return;
        }

        // Audio haptics arrive far faster than the grip motors can act on, so
        // a 5 ms window is collapsed into one peak-preserving frame.
        audioHapticsFrames_.fetch_add(1, std::memory_order_relaxed);
        const auto now = std::chrono::steady_clock::now();
        std::optional<DualSenseFeedback> expired;
        {
            std::lock_guard audioLock(audioMutex_);
            if (!pendingAudio_) {
                pendingAudio_ = feedback;
                audioWindowStarted_ = now;
            } else if (now - audioWindowStarted_ < kAudioWindow) {
                mergeAudio(*pendingAudio_, feedback);
                audioHapticsCoalesced_.fetch_add(1, std::memory_order_relaxed);
            } else {
                expired = pendingAudio_;
                pendingAudio_ = feedback;
                audioWindowStarted_ = now;
            }
        }
        if (expired) {
            deliverAudio(*expired);
        }
        audioCondition_.notify_one();
    }

    static void mergeAudio(DualSenseFeedback& target,
                           const DualSenseFeedback& source) noexcept {
        target.audioSequence = source.audioSequence;
        target.leftEnergy = (std::max)(target.leftEnergy, source.leftEnergy);
        target.rightEnergy = (std::max)(target.rightEnergy, source.rightEnergy);
        target.leftPeak = (std::max)(target.leftPeak, source.leftPeak);
        target.rightPeak = (std::max)(target.rightPeak, source.rightPeak);
        target.leftTransient = (std::max)(target.leftTransient, source.leftTransient);
        target.rightTransient = (std::max)(target.rightTransient, source.rightTransient);
    }

    void deliver(const DualSenseFeedback& feedback) noexcept {
        std::lock_guard handlerLock(handlerMutex_);
        if (!handler_) return;
        try {
            handler_(feedback);
        } catch (...) {
            // A bridge callback must never unwind across the libVIIPER C ABI.
        }
    }

    void deliverAudio(const DualSenseFeedback& feedback) noexcept {
        deliver(feedback);
        audioHapticsDelivered_.fetch_add(1, std::memory_order_relaxed);
    }

    void audioLoop() noexcept {
        std::unique_lock lock(audioMutex_);
        for (;;) {
            audioCondition_.wait(lock, [this] {
                return !audioWorkerRunning_ || pendingAudio_.has_value();
            });
            if (!audioWorkerRunning_) {
                auto finalAudio = std::move(pendingAudio_);
                pendingAudio_.reset();
                lock.unlock();
                if (finalAudio) deliverAudio(*finalAudio);
                return;
            }

            const auto deadline = audioWindowStarted_ + kAudioWindow;
            if (audioCondition_.wait_until(lock, deadline,
                                           [this] { return !audioWorkerRunning_; })) {
                continue;
            }
            if (std::chrono::steady_clock::now() < deadline || !pendingAudio_) {
                continue;
            }
            auto audio = std::move(pendingAudio_);
            pendingAudio_.reset();
            lock.unlock();
            deliverAudio(*audio);
            lock.lock();
        }
    }

    bool resolveFunctions(std::string& error) {
        return resolve(library_, "NewUSBServerASBLoopback", newUSBServerASBLoopback_, error) &&
               resolve(library_, "CloseUSBServer", closeUSBServer_, error) &&
               resolve(library_, "CreateUSBBus", createUSBBus_, error) &&
               resolve(library_, "RemoveUSBBus", removeUSBBus_, error) &&
               resolve(library_, "CreateDualSenseDevice", createDualSenseDevice_, error) &&
               resolve(library_, "SetDualSenseASBInputState", setInputState_, error) &&
               resolve(library_, "SetDualSenseASBRawOutputCallback", setRawOutputCallback_,
                       error) &&
               resolve(library_, "RemoveDualSenseDevice", removeDualSenseDevice_, error);
    }

    void clearFunctions() noexcept {
        newUSBServerASBLoopback_ = nullptr;
        closeUSBServer_ = nullptr;
        createUSBBus_ = nullptr;
        removeUSBBus_ = nullptr;
        createDualSenseDevice_ = nullptr;
        setInputState_ = nullptr;
        setRawOutputCallback_ = nullptr;
        removeDualSenseDevice_ = nullptr;
    }

    void clearHandler() noexcept {
        std::lock_guard handlerLock(handlerMutex_);
        handler_ = {};
    }

    void resetStats() noexcept {
        connected_.store(false, std::memory_order_relaxed);
        inputUpdates_.store(0, std::memory_order_relaxed);
        outputReports_.store(0, std::memory_order_relaxed);
        triggerReports_.store(0, std::memory_order_relaxed);
        rumbleReports_.store(0, std::memory_order_relaxed);
        audioHapticsFrames_.store(0, std::memory_order_relaxed);
        audioHapticsDelivered_.store(0, std::memory_order_relaxed);
        audioHapticsCoalesced_.store(0, std::memory_order_relaxed);
        malformedFrames_.store(0, std::memory_order_relaxed);
        unknownFrames_.store(0, std::memory_order_relaxed);
        initializationBootstrapUs_ = 0;
        initializationServerUs_ = 0;
        initializationBusUs_ = 0;
        initializationDeviceUs_ = 0;
        initializationFeedbackUs_ = 0;
        initializationInputUs_ = 0;
        backendVersion_.clear();
    }

    VirtualDualSenseOptions options_;
    void* library_ = nullptr;
    ViiperHandle serverHandle_ = 0;
    ViiperHandle deviceHandle_ = 0;
    std::uint32_t busId_ = 0;

    NewUSBServerFn newUSBServerASBLoopback_ = nullptr;
    CloseUSBServerFn closeUSBServer_ = nullptr;
    CreateUSBBusFn createUSBBus_ = nullptr;
    RemoveUSBBusFn removeUSBBus_ = nullptr;
    CreateDualSenseDeviceFn createDualSenseDevice_ = nullptr;
    SetDualSenseASBInputStateFn setInputState_ = nullptr;
    SetDualSenseASBRawOutputCallbackFn setRawOutputCallback_ = nullptr;
    RemoveDualSenseDeviceFn removeDualSenseDevice_ = nullptr;

    mutable std::mutex handlerMutex_;
    FeedbackHandler handler_;
    std::mutex inputMutex_;
    std::string dumpPath_;
    std::chrono::steady_clock::time_point dumpStartedAt_{};
    std::mutex callbackMutex_;
    std::atomic<bool> callbacksEnabled_{false};
    std::atomic<bool> connected_{false};

    std::mutex audioMutex_;
    std::condition_variable audioCondition_;
    std::thread audioWorker_;
    bool audioWorkerRunning_ = false;
    std::optional<DualSenseFeedback> pendingAudio_;
    std::chrono::steady_clock::time_point audioWindowStarted_{};

    std::atomic<std::uint64_t> inputUpdates_{0};
    std::atomic<std::uint64_t> outputReports_{0};
    std::atomic<std::uint64_t> triggerReports_{0};
    std::atomic<std::uint64_t> rumbleReports_{0};
    std::atomic<std::uint64_t> audioHapticsFrames_{0};
    std::atomic<std::uint64_t> audioHapticsDelivered_{0};
    std::atomic<std::uint64_t> audioHapticsCoalesced_{0};
    std::atomic<std::uint64_t> malformedFrames_{0};
    std::atomic<std::uint64_t> unknownFrames_{0};
    std::uint64_t initializationBootstrapUs_ = 0;
    std::uint64_t initializationServerUs_ = 0;
    std::uint64_t initializationBusUs_ = 0;
    std::uint64_t initializationDeviceUs_ = 0;
    std::uint64_t initializationFeedbackUs_ = 0;
    std::uint64_t initializationInputUs_ = 0;
    std::string backendVersion_;
};

} // namespace

std::filesystem::path resolveLibViiperLibraryPath(
    const VirtualDualSenseOptions& options) {
    if (!options.viiperLibrary.empty()) {
        return options.viiperLibrary;
    }
    return executableDirectory() / "libVIIPER.so";
}

std::unique_ptr<VirtualDualSense> createLibViiperVirtualDualSense(
    VirtualDualSenseOptions options) {
    return std::make_unique<LibViiperVirtualDualSense>(std::move(options));
}

} // namespace asb::dualsense

#endif // __linux__
