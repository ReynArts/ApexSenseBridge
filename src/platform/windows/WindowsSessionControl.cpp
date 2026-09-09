#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/SessionControl.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>

namespace asb::platform {
namespace {

std::wstring widenAscii(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

std::string windowsError(std::string_view operation, DWORD code) {
    return std::string(operation) + " failed with Windows error " +
           std::to_string(code) + '.';
}

class WindowsSessionControl final : public SessionControl {
public:
    WindowsSessionControl(HANDLE readyEvent,
                          HANDLE stopEvent,
                          HANDLE statusMapping,
                          SessionStatusBlock* status) noexcept
        : readyEvent_(readyEvent),
          stopEvent_(stopEvent),
          statusMapping_(statusMapping),
          status_(status) {}

    ~WindowsSessionControl() override {
        if (status_) UnmapViewOfFile(status_);
        if (statusMapping_) CloseHandle(statusMapping_);
        if (stopEvent_) CloseHandle(stopEvent_);
        if (readyEvent_) CloseHandle(readyEvent_);
    }

    bool publish(SessionPhase phase,
                 int exitCode,
                 std::string_view message,
                 std::string& error) noexcept override {
        SessionStatusBlock next{};
        next.phase = static_cast<std::uint16_t>(phase);
        next.exitCode = exitCode;
        const auto count = (std::min)(message.size(), next.message.size() - 1);
        next.messageLength = static_cast<std::uint32_t>(count);
        if (count != 0) {
            std::memcpy(next.message.data(), message.data(), count);
        }
        std::memcpy(status_, &next, sizeof(next));
        if (!FlushViewOfFile(status_, sizeof(next))) {
            error = windowsError("FlushViewOfFile(session status)", GetLastError());
            return false;
        }
        return true;
    }

    bool signalReady(std::string& error) noexcept override {
        if (!SetEvent(readyEvent_)) {
            error = windowsError("SetEvent(session ready)", GetLastError());
            return false;
        }
        return true;
    }

    [[nodiscard]] bool stopRequested() const noexcept override {
        return WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0;
    }

private:
    HANDLE readyEvent_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    HANDLE statusMapping_ = nullptr;
    SessionStatusBlock* status_ = nullptr;
};

constexpr wchar_t kGlobalStopEventName[] =
    L"Local\\ApexSenseBridge.ActiveSession.Stop.v1";
constexpr wchar_t kGlobalStoppedEventName[] =
    L"Local\\ApexSenseBridge.ActiveSession.Stopped.v1";
constexpr wchar_t kGlobalOwnerMutexName[] =
    L"Local\\ApexSenseBridge.ActiveSession.Owner.v1";
std::atomic_bool gGlobalSessionOwnerClaimed{false};

class WindowsGlobalSessionStop final : public GlobalSessionStop {
public:
    WindowsGlobalSessionStop(HANDLE ownerMutex,
                             HANDLE stopEvent,
                             HANDLE stoppedEvent) noexcept
        : ownerMutex_(ownerMutex), stopEvent_(stopEvent),
          stoppedEvent_(stoppedEvent) {}

    ~WindowsGlobalSessionStop() override {
        // Signal only after all objects declared after this guard have run their
        // destructors, so an uninstaller never races virtual-device teardown.
        if (stoppedEvent_) SetEvent(stoppedEvent_);
        if (stoppedEvent_) CloseHandle(stoppedEvent_);
        if (stopEvent_) CloseHandle(stopEvent_);
        if (ownerMutex_) {
            ReleaseMutex(ownerMutex_);
            CloseHandle(ownerMutex_);
        }
        gGlobalSessionOwnerClaimed.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool stopRequested() const noexcept override {
        return WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0;
    }

private:
    HANDLE ownerMutex_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    HANDLE stoppedEvent_ = nullptr;
};

} // namespace

std::unique_ptr<GlobalSessionStop> createGlobalSessionStop(std::string& error) {
    error.clear();
    bool expected = false;
    if (!gGlobalSessionOwnerClaimed.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        error = "Another ApexSenseBridge session is already active. Close either "
                "the Tray session or the Playnite-managed session before starting another.";
        return {};
    }

    HANDLE ownerMutex = CreateMutexW(
        nullptr, FALSE, kGlobalOwnerMutexName);
    if (!ownerMutex) {
        error = windowsError("CreateMutex(global session owner)", GetLastError());
        gGlobalSessionOwnerClaimed.store(false, std::memory_order_release);
        return {};
    }

    // Mutex existence alone does not mean that another engine owns it: the
    // Tray briefly opens the object while probing session state. Acquire it
    // atomically so that this probe cannot cause a spurious launch failure.
    const DWORD ownerWait = WaitForSingleObject(ownerMutex, 0);
    if (ownerWait == WAIT_TIMEOUT) {
        CloseHandle(ownerMutex);
        gGlobalSessionOwnerClaimed.store(false, std::memory_order_release);
        error = "Another ApexSenseBridge session is already active. Close either "
                "the Tray session or the Playnite-managed session before starting another.";
        return {};
    }
    if (ownerWait != WAIT_OBJECT_0 && ownerWait != WAIT_ABANDONED) {
        const auto waitError = GetLastError();
        CloseHandle(ownerMutex);
        gGlobalSessionOwnerClaimed.store(false, std::memory_order_release);
        error = windowsError("WaitForSingleObject(global session owner)", waitError);
        return {};
    }

    HANDLE stopEvent = CreateEventW(
        nullptr, TRUE, FALSE, kGlobalStopEventName);
    if (!stopEvent) {
        error = windowsError("CreateEvent(global session stop)", GetLastError());
        ReleaseMutex(ownerMutex);
        CloseHandle(ownerMutex);
        gGlobalSessionOwnerClaimed.store(false, std::memory_order_release);
        return {};
    }
    HANDLE stoppedEvent = CreateEventW(
        nullptr, TRUE, FALSE, kGlobalStoppedEventName);
    if (!stoppedEvent) {
        error = windowsError("CreateEvent(global session stopped)", GetLastError());
        CloseHandle(stopEvent);
        ReleaseMutex(ownerMutex);
        CloseHandle(ownerMutex);
        gGlobalSessionOwnerClaimed.store(false, std::memory_order_release);
        return {};
    }
    if (!ResetEvent(stopEvent) || !ResetEvent(stoppedEvent)) {
        error = windowsError("ResetEvent(global session control)", GetLastError());
        CloseHandle(stoppedEvent);
        CloseHandle(stopEvent);
        ReleaseMutex(ownerMutex);
        CloseHandle(ownerMutex);
        gGlobalSessionOwnerClaimed.store(false, std::memory_order_release);
        return {};
    }
    return std::make_unique<WindowsGlobalSessionStop>(
        ownerMutex, stopEvent, stoppedEvent);
}

bool requestGlobalSessionStop(
    std::chrono::milliseconds timeout, std::string& error) noexcept {
    error.clear();
    HANDLE stopEvent = OpenEventW(
        EVENT_MODIFY_STATE, FALSE, kGlobalStopEventName);
    if (!stopEvent) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND) return true;
        error = windowsError("OpenEvent(global session stop)", code);
        return false;
    }
    HANDLE stoppedEvent = OpenEventW(
        SYNCHRONIZE, FALSE, kGlobalStoppedEventName);
    if (!stoppedEvent) {
        error = windowsError("OpenEvent(global session stopped)", GetLastError());
        CloseHandle(stopEvent);
        return false;
    }

    const bool signalled = SetEvent(stopEvent) != FALSE;
    const DWORD signalError = signalled ? ERROR_SUCCESS : GetLastError();
    CloseHandle(stopEvent);
    if (!signalled) {
        error = windowsError("SetEvent(global session stop)", signalError);
        CloseHandle(stoppedEvent);
        return false;
    }

    const auto count = timeout.count() < 0
        ? 0ULL
        : static_cast<unsigned long long>(timeout.count());
    const DWORD waitMilliseconds = static_cast<DWORD>(
        (std::min)(count, static_cast<unsigned long long>(INFINITE - 1)));
    const DWORD waitResult = WaitForSingleObject(stoppedEvent, waitMilliseconds);
    CloseHandle(stoppedEvent);
    if (waitResult == WAIT_OBJECT_0) return true;
    if (waitResult == WAIT_TIMEOUT) {
        error = "Timed out waiting for the active bridge to detach and restore the controller.";
    } else {
        error = windowsError("WaitForSingleObject(global session stopped)", GetLastError());
    }
    return false;
}

std::unique_ptr<SessionControl> connectSessionControl(
    std::string_view token, std::string& error) {
    if (!isValidSessionToken(token)) {
        error = "The session token must contain exactly 32 hexadecimal characters.";
        return {};
    }

    const auto readyName = widenAscii(sessionReadyEventName(token));
    const auto stopName = widenAscii(sessionStopEventName(token));
    const auto statusName = widenAscii(sessionStatusMappingName(token));

    HANDLE readyEvent = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE,
                                   FALSE, readyName.c_str());
    if (!readyEvent) {
        error = windowsError("OpenEvent(session ready)", GetLastError());
        return {};
    }

    HANDLE stopEvent = OpenEventW(SYNCHRONIZE, FALSE, stopName.c_str());
    if (!stopEvent) {
        error = windowsError("OpenEvent(session stop)", GetLastError());
        CloseHandle(readyEvent);
        return {};
    }

    HANDLE statusMapping = OpenFileMappingW(
        FILE_MAP_READ | FILE_MAP_WRITE, FALSE, statusName.c_str());
    if (!statusMapping) {
        error = windowsError("OpenFileMapping(session status)", GetLastError());
        CloseHandle(stopEvent);
        CloseHandle(readyEvent);
        return {};
    }

    auto* status = static_cast<SessionStatusBlock*>(MapViewOfFile(
        statusMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, kSessionStatusSize));
    if (!status) {
        error = windowsError("MapViewOfFile(session status)", GetLastError());
        CloseHandle(statusMapping);
        CloseHandle(stopEvent);
        CloseHandle(readyEvent);
        return {};
    }

    return std::make_unique<WindowsSessionControl>(
        readyEvent, stopEvent, statusMapping, status);
}

} // namespace asb::platform
