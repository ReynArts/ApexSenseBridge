#ifdef NDEBUG
#undef NDEBUG
#endif

#include "cli/BridgeRuntimeSupport.h"

#include <cassert>
#include <chrono>
#include <thread>

namespace {
class FakeInput final : public asb::platform::PhysicalInputSource {
public:
    bool combined = true;
    bool independent = false;
    bool neutral = false;
    unsigned int polls = 0;
    unsigned int readyAfter = 3;
    asb::platform::PhysicalInputStatus status = asb::platform::PhysicalInputStatus::State;
    asb::platform::PhysicalInputStatus waitForState(
        asb::dualsense::DualSenseInputState& state,
        std::chrono::milliseconds wait, std::string& error) override {
        ++polls;
        if (status == asb::platform::PhysicalInputStatus::Timeout) std::this_thread::sleep_for(wait);
        if (status == asb::platform::PhysicalInputStatus::Error) error = "read failed";
        state.l2 = neutral ? 0 : 200;
        state.r2 = neutral ? 0 : 210;
        if (neutral) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return status;
    }
    std::string_view backendName() const noexcept override { return "fake"; }
    bool eventDriven() const noexcept override { return true; }
    asb::platform::PhysicalInputSourceStats stats() const noexcept override { return {}; }
    bool requiresIndependentTriggers() const noexcept override { return combined; }
    bool independentTriggersReady() const noexcept override { return independent && polls >= readyAfter; }
};
} // namespace

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

    FakeInput source;
    std::string validationError;
    asb::dualsense::DualSenseInputState latest{};
    source.combined = false;
    assert(asb::cli::validateIndependentTriggerStream(source, latest, 100ms, validationError));
    assert(source.polls == 0); // separate-trigger controllers remain unchanged
    source.combined = true;
    source.status = asb::platform::PhysicalInputStatus::Timeout;
    assert(!asb::cli::validateIndependentTriggerStream(source, latest, 5ms, validationError));
    assert(validationError.find("no live independent LT/RT") != std::string::npos);
    source.polls = 0;
    source.status = asb::platform::PhysicalInputStatus::State;
    assert(!asb::cli::validateIndependentTriggerStream(source, latest, 5ms, validationError));
    assert(source.polls > 0); // mapped reports alone cannot validate independent triggers
    source.polls = 0;
    source.independent = true;
    assert(asb::cli::validateIndependentTriggerStream(source, latest, 100ms, validationError));
    assert(source.polls == 3 && latest.l2 == 200 && latest.r2 == 210);
    assert(validationError.empty());
    source.status = asb::platform::PhysicalInputStatus::Error;
    assert(!asb::cli::validateIndependentTriggerStream(source, latest, 100ms, validationError));
    assert(validationError == "read failed");
    source.status = asb::platform::PhysicalInputStatus::Disconnected;
    assert(!asb::cli::validateIndependentTriggerStream(source, latest, 100ms, validationError));
    assert(validationError.find("disconnected") != std::string::npos);
    source.status = asb::platform::PhysicalInputStatus::State;
    source.neutral = true;
    source.independent = false;
    assert(!asb::cli::waitForPhysicalControlsReleased(source, 130ms));
    source.independent = true;
    assert(asb::cli::waitForPhysicalControlsReleased(source, 150ms));

    asb::platform::PhysicalInputSourceStats total{1, 2, 3, 4, 5, 6, 7, 8};
    asb::cli::accumulatePhysicalInputStats(total, {10, 20, 30, 40, 50, 60, 70, 80});
    assert(total.reports == 11 && total.timeouts == 22 && total.parseFailures == 33);
    assert(total.mappedReports == 44 && total.vendorReports == 55 && total.vendorStates == 66);
    assert(total.vendorParseFailures == 77 && total.vendorReadFailures == 88);
    return 0;
}
