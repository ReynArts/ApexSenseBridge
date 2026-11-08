#ifdef NDEBUG
#undef NDEBUG
#endif

#include "flydigi/Apex4WritePacing.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

using asb::flydigi::Apex4RumbleSpacing;
using asb::flydigi::Apex4WriteKind;
using asb::flydigi::Apex4WritePacer;
using asb::flydigi::apex4WriteSpacing;
using asb::flydigi::parseApex4RumbleSpacing;
using std::chrono::milliseconds;
using Clock = Apex4WritePacer::Clock;

const Clock::time_point kOrigin = Clock::time_point(std::chrono::seconds(1000));

Clock::time_point at(int ms) {
    return kOrigin + milliseconds(ms);
}

int msAt(Clock::time_point point) {
    return static_cast<int>(std::chrono::duration_cast<milliseconds>(point - kOrigin).count());
}

struct Event {
    int atMs;
    Apex4WriteKind kind;
    std::uint8_t value;
};

struct Written {
    int startMs;
    int endMs;
    Apex4WriteKind kind;
    std::uint8_t value;
};

struct Simulation {
    std::vector<Written> writes;
    std::uint64_t updates = 0;
    std::uint64_t coalesced = 0;
    std::uint64_t pendingAtEnd = 0;
};

Simulation simulate(const std::vector<Event>& events, milliseconds rumbleSpacing, int endMs) {
    constexpr int kWriteLatencyMs = 2;
    Apex4WritePacer pacer(rumbleSpacing);
    Simulation result;
    std::optional<std::uint8_t> pendingTrigger;
    std::optional<std::uint8_t> pendingRumble;
    std::size_t next = 0;
    int busyUntil = 0;
    for (int now = 0; now <= endMs; ++now) {
        while (next < events.size() && events[next].atMs == now) {
            const auto& event = events[next++];
            auto& pending = event.kind == Apex4WriteKind::Trigger ? pendingTrigger : pendingRumble;
            ++result.updates;
            if (pending) ++result.coalesced;
            pending = event.value;
        }
        if (now < busyUntil || (!pendingTrigger && !pendingRumble)) continue;
        const bool stopFirst = pendingRumble && *pendingRumble == 0;
        const auto kind = stopFirst || !pendingTrigger ? Apex4WriteKind::Rumble
                                                       : Apex4WriteKind::Trigger;
        if (at(now) < pacer.earliest(kind)) continue;
        auto& pending = kind == Apex4WriteKind::Trigger ? pendingTrigger : pendingRumble;
        const auto value = *pending;
        pending.reset();
        busyUntil = now + kWriteLatencyMs;
        pacer.record(kind, at(busyUntil));
        result.writes.push_back({now, busyUntil, kind, value});
    }
    result.pendingAtEnd = (pendingTrigger ? 1u : 0u) + (pendingRumble ? 1u : 0u);
    return result;
}

bool wrote(const Simulation& simulation, Apex4WriteKind kind, std::uint8_t value) {
    for (const auto& write : simulation.writes) {
        if (write.kind == kind && write.value == value) return true;
    }
    return false;
}

void assertSpacing(const Simulation& simulation, milliseconds rumbleSpacing) {
    for (std::size_t index = 1; index < simulation.writes.size(); ++index) {
        const auto& previous = simulation.writes[index - 1];
        const auto& current = simulation.writes[index];
        const int gap = current.startMs - previous.endMs;
        assert(gap >= 10);
        if (previous.kind == Apex4WriteKind::Rumble && current.kind == Apex4WriteKind::Rumble) {
            assert(gap >= rumbleSpacing.count());
        } else {
            assert(gap >= 25);
        }
    }
}

void assertCounters(const Simulation& simulation) {
    assert(simulation.updates ==
           simulation.writes.size() + simulation.coalesced + simulation.pendingAtEnd);
}

std::optional<std::uint8_t> lastRumble(const Simulation& simulation) {
    std::optional<std::uint8_t> value;
    for (const auto& write : simulation.writes) {
        if (write.kind == Apex4WriteKind::Rumble) value = write.value;
    }
    return value;
}

void testRumbleSpacingOverride() {
    const auto check = [](const char* text, long long expected, bool overridden) {
        const Apex4RumbleSpacing parsed = parseApex4RumbleSpacing(text);
        assert(parsed.spacing == milliseconds(expected));
        assert(parsed.overridden == overridden);
    };
    check(nullptr, 10, false);
    check("", 10, false);
    check("   ", 10, false);
    check("fast", 10, false);
    check("12ms", 10, false);
    check("1.5", 10, false);
    check("1", 10, true);
    check("0", 10, true);
    check("-20", 10, true);
    check("10", 10, true);
    check(" 15 \r\n", 15, true);
    check("25", 25, true);
    check("50", 50, true);
    check("51", 50, true);
    check("99999999999999999999999", 50, true);
    check("-99999999999999999999999", 10, true);
    assert(asb::flydigi::kApex4RumbleWriteSpacing == milliseconds(10));
    assert(asb::flydigi::kApex4TriggerWriteSpacing == milliseconds(25));
}

void testSpacingMatrix() {
    using enum Apex4WriteKind;
    for (const auto rumble : {milliseconds(1), milliseconds(10), milliseconds(17),
                              milliseconds(25), milliseconds(50), milliseconds(80)}) {
        const auto clamped = rumble < milliseconds(10) ? milliseconds(10)
                           : rumble > milliseconds(50) ? milliseconds(50) : rumble;
        assert(apex4WriteSpacing(Rumble, Rumble, rumble) == clamped);
        assert(apex4WriteSpacing(Rumble, Trigger, rumble) == milliseconds(25));
        assert(apex4WriteSpacing(Trigger, Rumble, rumble) == milliseconds(25));
        assert(apex4WriteSpacing(Trigger, Trigger, rumble) == milliseconds(25));
        assert(apex4WriteSpacing(None, Rumble, rumble) == milliseconds(25));
        assert(Apex4WritePacer(rumble).rumbleSpacing() == clamped);
    }

    Apex4WritePacer pacer;
    assert(pacer.lastKind() == None);
    assert(pacer.earliest(Rumble) == Clock::time_point{});
    assert(pacer.earliest(Trigger) == Clock::time_point{});
    pacer.record(Rumble, at(100));
    assert(msAt(pacer.earliest(Rumble)) == 110);
    assert(msAt(pacer.earliest(Trigger)) == 125);
    pacer.record(Trigger, at(130));
    assert(msAt(pacer.earliest(Rumble)) == 155);
    assert(msAt(pacer.earliest(Trigger)) == 155);

    Apex4WritePacer rollback(milliseconds(25));
    rollback.record(Rumble, at(0));
    assert(msAt(rollback.earliest(Rumble)) == 25);
    assert(msAt(rollback.earliest(Trigger)) == 25);
}

void testShortBurstIsWritten() {
    using enum Apex4WriteKind;
    const std::vector<Event> events{
        {0, Rumble, 60},
        {3, Rumble, 200},
        {18, Rumble, 0},
    };
    const auto current = simulate(events, milliseconds(10), 200);
    assert(wrote(current, Rumble, 200));
    assert(lastRumble(current) == std::uint8_t{0});
    assert(current.coalesced == 0);
    assertSpacing(current, milliseconds(10));
    assertCounters(current);

    const auto previous = simulate(events, milliseconds(25), 200);
    assert(!wrote(previous, Rumble, 200));
    assert(previous.coalesced == 1);
    assert(lastRumble(previous) == std::uint8_t{0});
    assertCounters(previous);
}

std::vector<Event> hapticBursts(std::size_t& bursts) {
    using enum Apex4WriteKind;
    std::uint32_t seed = 0x6837u;
    const auto random = [&seed](int low, int high) {
        seed = seed * 1664525u + 1013904223u;
        return low + static_cast<int>((seed >> 8) % static_cast<std::uint32_t>(high - low + 1));
    };
    std::vector<Event> events;
    bursts = 0;
    int now = 5;
    while (now < 5000) {
        const auto value = static_cast<std::uint8_t>(100 + (bursts % 150));
        const int duration = random(10, 20);
        events.push_back({now, Rumble, value});
        events.push_back({now + duration, Rumble, 0});
        ++bursts;
        now += duration + random(15, 60);
    }
    return events;
}

void testEveryHapticBurstReachesTheMotor() {
    using enum Apex4WriteKind;
    std::size_t bursts = 0;
    const auto events = hapticBursts(bursts);
    assert(bursts > 80);

    const auto current = simulate(events, milliseconds(10), 5200);
    std::size_t nonzero = 0;
    for (const auto& write : current.writes) {
        if (write.kind == Rumble && write.value != 0) ++nonzero;
    }
    assert(nonzero == bursts);
    assert(current.coalesced == 0);
    assert(lastRumble(current) == std::uint8_t{0});
    assertSpacing(current, milliseconds(10));
    assertCounters(current);

    const auto previous = simulate(events, milliseconds(25), 5200);
    std::size_t previousNonzero = 0;
    for (const auto& write : previous.writes) {
        if (write.kind == Rumble && write.value != 0) ++previousNonzero;
    }
    assert(previousNonzero < bursts);
    assert(previous.coalesced > 0);
    assert(lastRumble(previous) == std::uint8_t{0});
    assertSpacing(previous, milliseconds(25));
    assertCounters(previous);
}

void testTriggersKeepTheirSpacing() {
    using enum Apex4WriteKind;
    std::size_t bursts = 0;
    auto events = hapticBursts(bursts);
    std::vector<Event> mixed;
    std::size_t index = 0;
    for (int now = 0; now < 5000; now += 7) {
        while (index < events.size() && events[index].atMs <= now) mixed.push_back(events[index++]);
        if (now % 91 == 0) mixed.push_back({now, Trigger, static_cast<std::uint8_t>(now % 200)});
    }
    while (index < events.size()) mixed.push_back(events[index++]);

    for (const auto rumble : {milliseconds(10), milliseconds(25), milliseconds(50)}) {
        const auto simulation = simulate(mixed, rumble, 5400);
        std::size_t triggers = 0;
        for (const auto& write : simulation.writes) {
            if (write.kind == Trigger) ++triggers;
        }
        assert(triggers > 20);
        assertSpacing(simulation, rumble);
        assertCounters(simulation);
        assert(simulation.pendingAtEnd == 0);
        assert(lastRumble(simulation) == std::uint8_t{0});
    }
}

void testStopIsWrittenFirst() {
    using enum Apex4WriteKind;
    const std::vector<Event> events{
        {0, Rumble, 200},
        {1, Trigger, 40},
        {2, Rumble, 0},
    };
    const auto simulation = simulate(events, milliseconds(10), 200);
    assert(simulation.writes.size() == 3);
    assert(simulation.writes[0].kind == Rumble && simulation.writes[0].value == 200);
    assert(simulation.writes[1].kind == Rumble && simulation.writes[1].value == 0);
    assert(simulation.writes[1].startMs == simulation.writes[0].endMs + 10);
    assert(simulation.writes[2].kind == Trigger && simulation.writes[2].value == 40);
    assert(simulation.writes[2].startMs == simulation.writes[1].endMs + 25);
    assertCounters(simulation);
}

}

int main() {
    testRumbleSpacingOverride();
    testSpacingMatrix();
    testShortBurstIsWritten();
    testEveryHapticBurstReachesTheMotor();
    testTriggersKeepTheirSpacing();
    testStopIsWrittenFirst();
    std::cout << "APEX 4 write pacing tests passed\n";
    return 0;
}
