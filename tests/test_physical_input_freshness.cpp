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

    using Independent = asb::platform::IndependentTriggerStreamFreshness;
    Independent triggers;
    // Missing vendor states are not made fresh by an active mapped stream.
    assert(!triggers.ready(started));
    triggers.observe(started);
    triggers.observe(started + 1ms);
    triggers.observe(started + 2ms);
    assert(!triggers.ready(started + 20ms)); // one queued burst is not proof
    triggers.observe(started + 20ms);
    assert(triggers.ready(started + 20ms));
    assert(triggers.ready(started + 269ms));
    assert(!triggers.ready(started + 270ms)); // no cached LT+RT latch
    // After a stall, one late report does not immediately restore readiness.
    triggers.observe(started + 300ms);
    assert(!triggers.ready(started + 300ms));
    triggers.observe(started + 310ms);
    assert(!triggers.ready(started + 310ms));
    triggers.observe(started + 320ms);
    assert(triggers.ready(started + 320ms));

    using Loss = asb::platform::IndependentTriggerLossPolicy;
    Loss loss;
    assert(loss.streamStale(started) == Loss::Action::Continue);
    assert(loss.streamStale(started + 1499ms) == Loss::Action::Continue);
    loss.streamReady();
    assert(loss.streamStale(started + 2s) == Loss::Action::Continue);
    assert(loss.streamStale(started + 3500ms) == Loss::Action::Recover);
    loss.recoveryAttempted(started + 3500ms);
    assert(loss.recentRecoveries(started + 3500ms) == 1);
    assert(loss.streamStale(started + 2min) == Loss::Action::Continue);
    assert(loss.streamStale(started + 2min + 1500ms) == Loss::Action::Recover);
    loss.recoveryAttempted(started + 2min + 1500ms);
    assert(loss.streamStale(started + 3min) == Loss::Action::Continue);
    assert(loss.streamStale(started + 3min + 2s) == Loss::Action::Recover);
    loss.recoveryAttempted(started + 3min + 2s);
    assert(loss.streamStale(started + 4min) == Loss::Action::Continue);
    assert(loss.streamStale(started + 4min + 2s) == Loss::Action::Fail);
    assert(loss.recentRecoveries(started + 3500ms + 10min) == 2);
    assert(loss.streamStale(started + 14min) == Loss::Action::Recover);
    return 0;
}
