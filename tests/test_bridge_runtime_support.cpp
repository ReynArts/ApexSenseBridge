#ifdef NDEBUG
#undef NDEBUG
#endif

#include "cli/BridgeRuntimeSupport.h"

#include <cassert>
#include <chrono>

int main() {
    using namespace std::chrono_literals;

    asb::cli::MicrosecondLatencyHistogram histogram;
    assert(histogram.samples() == 0);
    assert(histogram.percentile(99) == 0);
    histogram.observe(-1us);
    histogram.observe(10us);
    histogram.observe(20us);
    histogram.observe(30us);
    histogram.observe(3ms);
    assert(histogram.samples() == 5);
    assert(histogram.percentile(50) == 20);
    assert(histogram.percentile(95) == 2000);
    assert(histogram.percentile(99) == 2000);

    asb::cli::ButtonHoldTracker holds;
    const auto start = asb::cli::ButtonHoldTracker::Clock::time_point{};
    holds.observe(false, start);
    holds.observe(true, start + 10ms);
    holds.observe(true, start + 40ms);
    holds.observe(false, start + 80ms);
    holds.observe(true, start + 100ms);
    holds.finish(start + 220ms);
    assert(holds.presses() == 2);
    assert(holds.maximumHoldMilliseconds() == 120);

    asb::dualsense::DualSenseInputState state{};
    assert(asb::cli::gameplayControlsReleased(state));
    state.l2 = 8;
    state.r2 = 8;
    assert(asb::cli::gameplayControlsReleased(state));
    state.l2 = 9;
    assert(!asb::cli::gameplayControlsReleased(state));
    state = {};
    state.buttons = 1;
    assert(!asb::cli::gameplayControlsReleased(state));
    state = {};
    state.dpad = 1;
    assert(!asb::cli::gameplayControlsReleased(state));

    assert(asb::cli::logicalProcessorCount() >= 1);
    return 0;
}
