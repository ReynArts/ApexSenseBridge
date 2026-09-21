#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/PhysicalInputFreshnessWatchdog.h"

#include <cassert>
#include <chrono>

int main() {
    using Watchdog = asb::platform::PhysicalInputFreshnessWatchdog;
    using namespace std::chrono_literals;

    const auto started = Watchdog::Clock::time_point{};
    Watchdog watchdog(1s, started);
    assert(!watchdog.expired(started + 999ms));
    assert(watchdog.expired(started + 1s));

    watchdog.observeFreshState(started + 750ms);
    assert(!watchdog.expired(started + 1749ms));
    assert(watchdog.expired(started + 1750ms));
    return 0;
}
