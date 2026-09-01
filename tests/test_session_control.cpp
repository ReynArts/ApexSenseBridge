#ifdef NDEBUG
#undef NDEBUG
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/SessionControl.h"

#include <cassert>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>

namespace {

std::wstring widen(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

} // namespace

int main() {
    using namespace asb::platform;

    constexpr std::string_view token = "0123456789abcdef0123456789abcdef";
    assert(isValidSessionToken(token));
    assert(!isValidSessionToken("short"));
    assert(!isValidSessionToken("0123456789abcdef0123456789abcdeg"));
    assert(sessionReadyEventName(token) ==
           "Local\\ApexSenseBridge.Session.0123456789abcdef0123456789abcdef.Ready");

    const auto readyName = widen(sessionReadyEventName(token));
    const auto stopName = widen(sessionStopEventName(token));
    const auto statusName = widen(sessionStatusMappingName(token));
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str());
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, stopName.c_str());
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                        PAGE_READWRITE, 0,
                                        static_cast<DWORD>(kSessionStatusSize),
                                        statusName.c_str());
    assert(ready && stop && mapping);

    auto* status = static_cast<SessionStatusBlock*>(MapViewOfFile(
        mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, kSessionStatusSize));
    assert(status);
    std::memset(status, 0, kSessionStatusSize);

    std::string error;
    auto control = connectSessionControl(token, error);
    assert(control);
    assert(error.empty());
    assert(control->publish(SessionPhase::Starting, 0,
                            "Bridge initialization started.", error));
    assert(status->magic == kSessionStatusMagic);
    assert(status->protocolVersion == kSessionProtocolVersion);
    assert(status->phase == static_cast<std::uint16_t>(SessionPhase::Starting));
    assert(status->exitCode == 0);
    assert(std::string_view(status->message.data(), status->messageLength) ==
           "Bridge initialization started.");

    assert(control->publish(SessionPhase::Ready, 0, "Bridge ready.", error));
    assert(control->signalReady(error));
    assert(WaitForSingleObject(ready, 0) == WAIT_OBJECT_0);
    assert(status->phase == static_cast<std::uint16_t>(SessionPhase::Ready));

    assert(!control->stopRequested());
    assert(SetEvent(stop));
    assert(control->stopRequested());

    const std::string oversized(700, 'x');
    assert(control->publish(SessionPhase::Failed, 42, oversized, error));
    assert(status->phase == static_cast<std::uint16_t>(SessionPhase::Failed));
    assert(status->exitCode == 42);
    assert(status->messageLength == kSessionMessageCapacity - 1);
    assert(status->message.back() == '\0');

    control.reset();
    UnmapViewOfFile(status);
    CloseHandle(mapping);
    CloseHandle(stop);
    CloseHandle(ready);

    // The maintenance stop waits for the bridge guard to be destroyed, which
    // models cleanup completing before the uninstaller's taskkill fallback.
    assert(requestGlobalSessionStop(std::chrono::milliseconds(0), error));
    auto globalStop = createGlobalSessionStop(error);
    assert(globalStop);
    assert(!globalStop->stopRequested());
    std::atomic<bool> maintenanceResult = false;
    std::thread maintenance([&]() {
        std::string maintenanceError;
        maintenanceResult.store(
            requestGlobalSessionStop(
                std::chrono::milliseconds(2000), maintenanceError),
            std::memory_order_relaxed);
    });
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(1000);
    while (!globalStop->stopRequested() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    assert(globalStop->stopRequested());
    globalStop.reset();
    maintenance.join();
    assert(maintenanceResult.load(std::memory_order_relaxed));
    return 0;
}
