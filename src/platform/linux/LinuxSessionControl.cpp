#ifdef __linux__

#include "platform/SessionControl.h"

#include "platform/linux/LinuxPaths.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

namespace asb::platform {
namespace {

namespace fs = std::filesystem;

fs::path sessionFile(std::string_view name) {
    return linux_paths::runtimeDirectory() / std::string(name);
}

fs::path ownerLockPath() {
    return linux_paths::runtimeDirectory() / "session.lock";
}

std::string errnoMessage(const char* what, int code) {
    return std::string(what) + " failed (errno " + std::to_string(code) + ": " +
           std::strerror(code) + ")";
}

class LinuxSessionControl final : public SessionControl {
public:
    LinuxSessionControl(int statusDescriptor,
                        SessionStatusBlock* status,
                        fs::path readyPath,
                        fs::path stopPath)
        : statusDescriptor_(statusDescriptor),
          status_(status),
          readyPath_(std::move(readyPath)),
          stopPath_(std::move(stopPath)) {}

    ~LinuxSessionControl() override {
        if (status_) {
            ::munmap(status_, kSessionStatusSize);
        }
        if (statusDescriptor_ >= 0) {
            ::close(statusDescriptor_);
        }
    }

    bool publish(SessionPhase phase,
                 int exitCode,
                 std::string_view message,
                 std::string& error) noexcept override {
        if (!status_) {
            error = "The session status block is not mapped.";
            return false;
        }
        status_->magic = kSessionStatusMagic;
        status_->protocolVersion = kSessionProtocolVersion;
        status_->phase = static_cast<std::uint16_t>(phase);
        status_->exitCode = exitCode;
        const auto length = (std::min)(message.size(), kSessionMessageCapacity);
        std::memcpy(status_->message.data(), message.data(), length);
        if (length < kSessionMessageCapacity) {
            status_->message[length] = '\0';
        }
        status_->messageLength = static_cast<std::uint32_t>(length);
        if (::msync(status_, kSessionStatusSize, MS_SYNC) != 0) {
            error = errnoMessage("msync", errno);
            return false;
        }
        return true;
    }

    bool signalReady(std::string& error) noexcept override {
        // Opening the write end unblocks the owner's blocking read open, which
        // is exactly the SetEvent/WaitForSingleObject handshake Windows uses.
        const int writer = ::open(readyPath_.c_str(), O_WRONLY | O_CLOEXEC | O_NONBLOCK);
        if (writer < 0) {
            if (errno == ENXIO) {
                // Nobody is waiting yet; the owner reads the status block
                // instead, so this is not a failure.
                return true;
            }
            error = errnoMessage("open(ready fifo)", errno);
            return false;
        }
        const char token = 'R';
        [[maybe_unused]] const ssize_t written = ::write(writer, &token, 1);
        ::close(writer);
        return true;
    }

    [[nodiscard]] bool stopRequested() const noexcept override {
        std::error_code ignored;
        return fs::exists(stopPath_, ignored);
    }

private:
    int statusDescriptor_ = -1;
    SessionStatusBlock* status_ = nullptr;
    fs::path readyPath_;
    fs::path stopPath_;
};

class LinuxGlobalSessionStop final : public GlobalSessionStop {
public:
    explicit LinuxGlobalSessionStop(int descriptor) : descriptor_(descriptor) {}

    ~LinuxGlobalSessionStop() override {
        if (descriptor_ >= 0) {
            ::flock(descriptor_, LOCK_UN);
            ::close(descriptor_);
            std::error_code ignored;
            fs::remove(ownerLockPath(), ignored);
        }
    }

    [[nodiscard]] bool stopRequested() const noexcept override {
        // SIGTERM already sets the CLI stop flag; this covers the explicit
        // stop-active-sessions handshake.
        std::error_code ignored;
        return fs::exists(linux_paths::runtimeDirectory() / "stop-all", ignored);
    }

private:
    int descriptor_ = -1;
};

} // namespace

std::unique_ptr<GlobalSessionStop> createGlobalSessionStop(std::string& error) {
    if (!linux_paths::ensureDirectory(linux_paths::runtimeDirectory(), error)) {
        return {};
    }
    const int descriptor = ::open(ownerLockPath().c_str(),
                                  O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (descriptor < 0) {
        error = errnoMessage("open(session.lock)", errno);
        return {};
    }
    if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        const int code = errno;
        ::close(descriptor);
        if (code == EWOULDBLOCK) {
            error = "Another ApexSenseBridge session is already active.";
        } else {
            error = errnoMessage("flock(session.lock)", code);
        }
        return {};
    }

    // The pid is what stop-active-sessions signals. The kernel releases the
    // lock on crash, so a stale pid can never block a new session.
    if (::ftruncate(descriptor, 0) == 0) {
        const auto pid = std::to_string(::getpid());
        [[maybe_unused]] const ssize_t written =
            ::write(descriptor, pid.c_str(), pid.size());
    }
    return std::make_unique<LinuxGlobalSessionStop>(descriptor);
}

bool requestGlobalSessionStop(
    std::chrono::milliseconds timeout, std::string& error) noexcept {
    const auto lockPath = ownerLockPath();
    std::error_code ignored;
    if (!fs::exists(lockPath, ignored)) {
        return true;
    }

    const int descriptor = ::open(lockPath.c_str(), O_RDWR | O_CLOEXEC);
    if (descriptor < 0) {
        if (errno == ENOENT) {
            return true;
        }
        error = errnoMessage("open(session.lock)", errno);
        return false;
    }

    if (::flock(descriptor, LOCK_EX | LOCK_NB) == 0) {
        // Nothing holds it, so no session is running.
        ::flock(descriptor, LOCK_UN);
        ::close(descriptor);
        return true;
    }

    char buffer[32] = {};
    const ssize_t read = ::pread(descriptor, buffer, sizeof(buffer) - 1, 0);
    ::close(descriptor);
    if (read <= 0) {
        error = "An active session holds the lock but published no process id.";
        return false;
    }

    const auto pid = static_cast<pid_t>(std::strtol(buffer, nullptr, 10));
    if (pid <= 0) {
        error = "An active session published an invalid process id.";
        return false;
    }
    if (::kill(pid, SIGTERM) != 0 && errno != ESRCH) {
        error = errnoMessage("kill(SIGTERM)", errno);
        return false;
    }

    // SIGTERM runs the normal stop path, which restores triggers, rumble and
    // controller visibility before the process exits and drops the lock.
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const int probe = ::open(lockPath.c_str(), O_RDWR | O_CLOEXEC);
        if (probe < 0) {
            return true;
        }
        const bool free = ::flock(probe, LOCK_EX | LOCK_NB) == 0;
        if (free) {
            ::flock(probe, LOCK_UN);
        }
        ::close(probe);
        if (free) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    error = "The active bridge did not exit within the timeout.";
    return false;
}

std::unique_ptr<SessionControl> connectSessionControl(
    std::string_view token, std::string& error) {
    if (!isValidSessionToken(token)) {
        error = "The session token must be 32 hexadecimal characters.";
        return {};
    }
    if (!linux_paths::ensureDirectory(linux_paths::runtimeDirectory(), error)) {
        return {};
    }

    const auto statusPath = sessionFile(sessionStatusMappingName(token));
    const int descriptor = ::open(statusPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (descriptor < 0) {
        error = errnoMessage("open(session status)", errno);
        return {};
    }

    struct stat info {};
    if (::fstat(descriptor, &info) != 0) {
        error = errnoMessage("fstat(session status)", errno);
        ::close(descriptor);
        return {};
    }
    if (info.st_size != static_cast<off_t>(kSessionStatusSize) &&
        ::ftruncate(descriptor, static_cast<off_t>(kSessionStatusSize)) != 0) {
        error = errnoMessage("ftruncate(session status)", errno);
        ::close(descriptor);
        return {};
    }

    void* mapping = ::mmap(nullptr, kSessionStatusSize, PROT_READ | PROT_WRITE,
                           MAP_SHARED, descriptor, 0);
    if (mapping == MAP_FAILED) {
        error = errnoMessage("mmap(session status)", errno);
        ::close(descriptor);
        return {};
    }

    auto* status = static_cast<SessionStatusBlock*>(mapping);
    // Fail closed on a block that exists but belongs to something else.
    if (status->magic != 0 && status->magic != kSessionStatusMagic) {
        error = "The session status block has an unexpected signature.";
        ::munmap(mapping, kSessionStatusSize);
        ::close(descriptor);
        return {};
    }

    const auto readyPath = sessionFile(sessionReadyEventName(token));
    std::error_code ignored;
    if (!fs::exists(readyPath, ignored) &&
        ::mkfifo(readyPath.c_str(), 0600) != 0 && errno != EEXIST) {
        error = errnoMessage("mkfifo(ready)", errno);
        ::munmap(mapping, kSessionStatusSize);
        ::close(descriptor);
        return {};
    }

    return std::make_unique<LinuxSessionControl>(
        descriptor, status, readyPath, sessionFile(sessionStopEventName(token)));
}

} // namespace asb::platform

#endif // __linux__
