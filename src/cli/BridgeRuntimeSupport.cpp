#include "cli/BridgeRuntimeSupport.h"

#include <algorithm>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace asb::cli {

ProcessUsageSnapshot processUsageSnapshot() noexcept {
    ProcessUsageSnapshot snapshot{};
#ifdef _WIN32
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        ULARGE_INTEGER kernelValue{};
        kernelValue.LowPart = kernel.dwLowDateTime;
        kernelValue.HighPart = kernel.dwHighDateTime;
        ULARGE_INTEGER userValue{};
        userValue.LowPart = user.dwLowDateTime;
        userValue.HighPart = user.dwHighDateTime;
        snapshot.cpu100ns = kernelValue.QuadPart + userValue.QuadPart;
    }
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        snapshot.workingSetBytes = counters.WorkingSetSize;
        snapshot.peakWorkingSetBytes = counters.PeakWorkingSetSize;
    }
#endif
    return snapshot;
}

unsigned int logicalProcessorCount() noexcept {
#ifdef _WIN32
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return (std::max)(1U, static_cast<unsigned int>(info.dwNumberOfProcessors));
#else
    return (std::max)(1U, std::thread::hardware_concurrency());
#endif
}

bool gameplayControlsReleased(
    const asb::dualsense::DualSenseInputState& state) noexcept {
    constexpr std::uint8_t kTriggerReleaseThreshold = 8;
    return state.buttons == 0 && state.dpad == 0 &&
           state.l2 <= kTriggerReleaseThreshold &&
           state.r2 <= kTriggerReleaseThreshold;
}

bool waitForPhysicalControlsReleased(
    asb::platform::PhysicalInputSource& input,
    std::chrono::milliseconds maximumWait) noexcept {
    constexpr auto kStableRelease = std::chrono::milliseconds(120);
    const auto deadline = std::chrono::steady_clock::now() + maximumWait;
    std::optional<std::chrono::steady_clock::time_point> releasedAt;
    while (std::chrono::steady_clock::now() < deadline) {
        asb::dualsense::DualSenseInputState state{};
        std::string error;
        const auto status = input.waitForState(
            state, input.eventDriven() ? std::chrono::milliseconds(25)
                                       : std::chrono::milliseconds(1),
            error);
        const auto now = std::chrono::steady_clock::now();
        if (status == asb::platform::PhysicalInputStatus::State) {
            if (gameplayControlsReleased(state)) {
                if (!releasedAt) releasedAt = now;
                if (now - *releasedAt >= kStableRelease) return true;
            } else {
                releasedAt.reset();
            }
        } else if (status == asb::platform::PhysicalInputStatus::Disconnected ||
                   status == asb::platform::PhysicalInputStatus::Error) {
            return false;
        }
    }
    return false;
}

} // namespace asb::cli
