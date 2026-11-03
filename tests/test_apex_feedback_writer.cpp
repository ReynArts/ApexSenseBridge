#ifdef NDEBUG
#undef NDEBUG
#endif

#include "flydigi/Apex4Protocol.h"
#include "flydigi/Apex5Device.h"
#include "flydigi/Apex5Protocol.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using asb::TriggerMode;
using asb::TriggerSide;
using asb::flydigi::Apex5Device;
using asb::flydigi::TransportPtr;

struct RecordedWrite {
    Clock::time_point at;
    std::vector<std::uint8_t> bytes;
};

class RecordingTransport final : public asb::platform::HidTransport {
public:
    explicit RecordingTransport(bool apex4, bool answerIdentity = true)
        : apex4_(apex4), answerIdentity_(answerIdentity) {
        info_.vendorId = apex4 ? asb::flydigi::kApex4VendorId : asb::flydigi::kVendorId;
        info_.productId = apex4 ? asb::flydigi::kApex4ProductId : 0x2501;
        info_.usagePage = asb::flydigi::kVendorUsagePage;
        info_.inputReportLength = 32;
        info_.outputReportLength = 32;
    }

    bool isOpen() const noexcept override { return true; }
    const asb::HidDeviceInfo& info() const noexcept override { return info_; }

    bool writeOutputReport(std::span<const std::uint8_t> report, std::string&) override {
        std::function<void()> hook;
        {
            std::lock_guard lock(mutex_);
            writes_.push_back({Clock::now(), {report.begin(), report.end()}});
            queueReply(report);
            hook = afterWrite_;
        }
        if (hook) hook();
        return true;
    }

    asb::platform::HidReadStatus readInputReport(
        std::span<std::uint8_t> report, std::chrono::milliseconds,
        std::size_t& bytesRead, std::string&) override {
        std::lock_guard lock(mutex_);
        bytesRead = 0;
        if (replies_.empty()) return asb::platform::HidReadStatus::Timeout;
        const auto reply = replies_.front();
        replies_.pop_front();
        bytesRead = (std::min)(report.size(), reply.size());
        std::copy_n(reply.begin(), bytesRead, report.begin());
        return asb::platform::HidReadStatus::Data;
    }

    void setAfterWrite(std::function<void()> hook) {
        std::lock_guard lock(mutex_);
        afterWrite_ = std::move(hook);
    }
    std::vector<RecordedWrite> writes() const {
        std::lock_guard lock(mutex_);
        return writes_;
    }
    std::size_t writeCount() const {
        std::lock_guard lock(mutex_);
        return writes_.size();
    }
    bool waitForWrites(std::size_t count) const {
        const auto deadline = Clock::now() + std::chrono::seconds(2);
        while (writeCount() < count) {
            if (Clock::now() >= deadline) return false;
            std::this_thread::sleep_for(milliseconds(1));
        }
        return true;
    }

private:
    void queueReply(std::span<const std::uint8_t> report) {
        std::array<std::uint8_t, 32> reply{};
        if (apex4_) {
            if (!answerIdentity_ || report.size() < 2 ||
                report[1] != asb::flydigi::kApex4CmdGetInfo) {
                return;
            }
            reply[3] = 84;
            reply[15] = asb::flydigi::kApex4CmdGetInfo;
            replies_.push_back(reply);
            return;
        }
        if (report.size() < 4 || report[0] != asb::flydigi::kReportIdOut ||
            report[1] != asb::flydigi::kMagic0 || report[2] != asb::flydigi::kMagic1) {
            return;
        }
        const auto command = report[3];
        if (command == asb::flydigi::kCmdGetInfo) {
            if (!answerIdentity_) return;
            reply[4] = 1;
            reply[6] = 128;
            reply[7] = 2;
        } else if (command != asb::flydigi::kCmdWriteRgbStart &&
                   command != asb::flydigi::kCmdWriteRgbPack) {
            return;
        }
        reply[0] = asb::flydigi::kReportIdIn;
        reply[1] = asb::flydigi::kMagic0;
        reply[2] = asb::flydigi::kMagic1;
        reply[3] = command;
        replies_.push_back(reply);
    }

    bool apex4_;
    bool answerIdentity_;
    asb::HidDeviceInfo info_{};
    mutable std::mutex mutex_;
    std::vector<RecordedWrite> writes_;
    std::deque<std::array<std::uint8_t, 32>> replies_;
    std::function<void()> afterWrite_;
};

bool isRumbleWrite(bool apex4, const std::vector<std::uint8_t>& bytes) {
    return apex4 ? bytes.size() > 1 && bytes[1] == asb::flydigi::kApex4CmdRumble
                 : bytes.size() > 3 && bytes[3] == asb::flydigi::kCmdSetRumble;
}

bool isTriggerWrite(bool apex4, const std::vector<std::uint8_t>& bytes) {
    return apex4 ? bytes.size() > 1 && bytes[1] == asb::flydigi::kApex4CmdSetForceTriggerDInput
                 : bytes.size() > 3 && bytes[3] == asb::flydigi::kCmdSetForceTrigger;
}

bool hasCommand(const RecordedWrite& write, std::uint8_t command) {
    return write.bytes.size() > 3 && write.bytes[3] == command;
}

void testContinuousTriggersDoNotStarveRumble(bool apex4) {
    auto* transport = new RecordingTransport(apex4);
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    const auto policy = device.asyncWritePolicy();
    assert(policy.has_value());
    assert(device.startAsyncWrites(error));
    assert(device.asyncWritesRunning());
    const auto baseline = transport->writeCount();

    std::atomic<unsigned> updates{0};
    const auto feed = [&device, &updates] {
        const auto value = static_cast<std::uint8_t>(++updates);
        std::string ignored;
        assert(device.queueTriggerRaw({TriggerSide::Left, TriggerMode::Race, {25, value}}, ignored));
        assert(device.queueTriggerRaw({TriggerSide::Right, TriggerMode::Race, {25, value}}, ignored));
        assert(device.queueRumble(value, 7, ignored));
    };
    const auto firstRumbleQueuedAt = Clock::now();
    const auto feedUntil = firstRumbleQueuedAt + milliseconds(700);
    transport->setAfterWrite([&feed, feedUntil] {
        if (Clock::now() < feedUntil) feed();
    });
    feed();
    std::this_thread::sleep_until(feedUntil + milliseconds(50));
    transport->setAfterWrite({});
    device.stopAsyncWrites();

    const auto writes = transport->writes();
    std::optional<Clock::time_point> firstRumble;
    std::size_t triggersBeforeRumble = 0;
    std::size_t rumblesWhileFeeding = 0;
    for (std::size_t index = baseline; index < writes.size(); ++index) {
        const auto& write = writes[index];
        if (isRumbleWrite(apex4, write.bytes)) {
            if (!firstRumble) firstRumble = write.at;
            if (write.at < feedUntil) ++rumblesWhileFeeding;
        } else if (!firstRumble) {
            assert(isTriggerWrite(apex4, write.bytes));
            ++triggersBeforeRumble;
        }
    }
    assert(firstRumble.has_value());
    assert(triggersBeforeRumble >= 2);
    assert(*firstRumble - firstRumbleQueuedAt >= milliseconds(100));
    assert(*firstRumble - firstRumbleQueuedAt <=
           milliseconds(200) + 3 * policy->spacing);
    assert(rumblesWhileFeeding >= 2);
}

bool isApex4RumbleStop(const RecordedWrite& write) {
    return isRumbleWrite(true, write.bytes) && write.bytes.size() > 3 &&
           write.bytes[2] == 0 && write.bytes[3] == 0;
}

void testApex4RumbleStopSkipsTriggerQueue() {
    auto* transport = new RecordingTransport(true);
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    const auto policy = device.asyncWritePolicy();
    assert(policy.has_value() && policy->spacing == milliseconds(10) && policy->rumbleStopFirst);
    assert(device.startAsyncWrites(error));
    const auto baseline = transport->writeCount();

    std::atomic<bool> feeding{true};
    std::atomic<unsigned> updates{0};
    const auto feedTriggers = [&device, &updates] {
        const auto value = static_cast<std::uint8_t>(++updates);
        std::string ignored;
        assert(device.queueTriggerRaw({TriggerSide::Left, TriggerMode::Race, {25, value}}, ignored));
        assert(device.queueTriggerRaw({TriggerSide::Right, TriggerMode::Race, {25, value}}, ignored));
    };
    transport->setAfterWrite([&feeding, &feedTriggers] {
        if (feeding.load(std::memory_order_relaxed)) feedTriggers();
    });
    feedTriggers();
    assert(device.queueRumble(200, 100, error));
    const auto waitFor = [&transport, baseline](auto&& predicate) {
        const auto deadline = Clock::now() + std::chrono::seconds(2);
        for (;;) {
            const auto writes = transport->writes();
            for (std::size_t index = baseline; index < writes.size(); ++index) {
                if (predicate(writes[index])) return true;
            }
            if (Clock::now() >= deadline) return false;
            std::this_thread::sleep_for(milliseconds(1));
        }
    };
    assert(waitFor([](const RecordedWrite& write) { return isRumbleWrite(true, write.bytes); }));
    const auto stopQueuedAt = Clock::now();
    assert(device.queueRumble(0, 0, error));
    assert(waitFor(isApex4RumbleStop));
    feeding.store(false, std::memory_order_relaxed);
    transport->setAfterWrite({});
    device.stopAsyncWrites();

    const auto writes = transport->writes();
    std::optional<Clock::time_point> stopAt;
    std::size_t triggerWrites = 0;
    for (std::size_t index = baseline; index < writes.size(); ++index) {
        if (isTriggerWrite(true, writes[index].bytes)) ++triggerWrites;
        if (!stopAt && isApex4RumbleStop(writes[index])) stopAt = writes[index].at;
    }
    assert(stopAt.has_value());
    assert(triggerWrites >= 4);
    assert(*stopAt - stopQueuedAt < milliseconds(75));
}

bool isApex4Rumble(const RecordedWrite& write, std::uint8_t low, std::uint8_t high) {
    return isRumbleWrite(true, write.bytes) && write.bytes.size() > 3 &&
           write.bytes[2] == low && write.bytes[3] == high;
}

bool waitUntil(const std::function<bool()>& predicate) {
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (!predicate()) {
        if (Clock::now() >= deadline) return false;
        std::this_thread::sleep_for(milliseconds(1));
    }
    return true;
}

void testApex4ShortBurstReachesWriter() {
    auto* transport = new RecordingTransport(true);
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    assert(device.startAsyncWrites(error));
    const auto baseline = transport->writeCount();

    std::atomic<bool> armed{true};
    std::atomic<Clock::rep> firstWriteAt{0};
    transport->setAfterWrite([&device, &armed, &firstWriteAt] {
        if (!armed.exchange(false)) return;
        firstWriteAt.store(Clock::now().time_since_epoch().count());
        std::string ignored;
        assert(device.queueRumble(200, 180, ignored));
    });
    assert(device.queueRumble(40, 30, error));
    assert(waitUntil([&firstWriteAt] { return firstWriteAt.load() != 0; }));
    const auto burstEndsAt = Clock::time_point(Clock::duration(firstWriteAt.load())) +
                             milliseconds(23);
    while (Clock::now() < burstEndsAt) std::this_thread::yield();
    assert(device.queueRumble(0, 0, error));
    assert(waitUntil([&transport, baseline] {
        const auto writes = transport->writes();
        return writes.size() > baseline && isApex4RumbleStop(writes.back());
    }));
    transport->setAfterWrite({});
    device.stopAsyncWrites();

    const auto all = transport->writes();
    const std::vector<RecordedWrite> writes(all.begin() + static_cast<std::ptrdiff_t>(baseline),
                                            all.end());
    assert(writes.size() == 3);
    assert(isApex4Rumble(writes[0], 40, 30));
    assert(isApex4Rumble(writes[1], 200, 180));
    assert(isApex4RumbleStop(writes[2]));
    assert(writes[1].at - writes[0].at >= milliseconds(10));
    assert(writes[2].at - writes[1].at >= milliseconds(10));
    const auto stats = device.asyncWriteStats();
    assert(stats.writes == 3);
    assert(stats.coalesced == 0);
}

void testApex4PacingAndCounters() {
    auto* transport = new RecordingTransport(true);
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    assert(device.startAsyncWrites(error));
    const auto baseline = transport->writeCount();

    std::atomic<bool> feeding{true};
    std::atomic<unsigned> updates{0};
    std::atomic<unsigned> step{0};
    const auto feed = [&device, &updates, &step] {
        const unsigned current = ++step;
        std::string ignored;
        const auto value = static_cast<std::uint8_t>(1 + current % 200);
        if (current % 5 == 0) {
            assert(device.queueTriggerRaw({current % 10 == 0 ? TriggerSide::Left : TriggerSide::Right,
                                           TriggerMode::Race, {25, value}}, ignored));
            ++updates;
        }
        assert(device.queueRumble(current % 3 == 0 ? 0 : value, current % 3 == 0 ? 0 : 9,
                                  ignored));
        ++updates;
    };
    transport->setAfterWrite([&feeding, &feed] {
        if (feeding.load(std::memory_order_relaxed)) feed();
    });
    feed();
    std::this_thread::sleep_for(milliseconds(600));
    feeding.store(false, std::memory_order_relaxed);
    transport->setAfterWrite({});
    std::this_thread::sleep_for(milliseconds(150));
    assert(device.queueRumble(0, 0, error));
    ++updates;
    assert(waitUntil([&transport, baseline] {
        const auto writes = transport->writes();
        return writes.size() > baseline && isApex4RumbleStop(writes.back());
    }));
    std::this_thread::sleep_for(milliseconds(60));
    device.stopAsyncWrites();

    const auto all = transport->writes();
    const std::vector<RecordedWrite> writes(all.begin() + static_cast<std::ptrdiff_t>(baseline),
                                            all.end());
    std::size_t rumblePairs = 0;
    std::size_t triggerNeighbours = 0;
    for (std::size_t index = 1; index < writes.size(); ++index) {
        const auto gap = writes[index].at - writes[index - 1].at;
        const bool rumblePair = isRumbleWrite(true, writes[index].bytes) &&
                                isRumbleWrite(true, writes[index - 1].bytes);
        assert(gap >= milliseconds(10));
        if (rumblePair) {
            ++rumblePairs;
        } else {
            ++triggerNeighbours;
            assert(gap >= milliseconds(25));
        }
    }
    assert(rumblePairs > 0);
    assert(triggerNeighbours > 0);
    assert(isApex4RumbleStop(writes.back()));
    const auto stats = device.asyncWriteStats();
    assert(stats.writes == writes.size());
    assert(stats.coalesced + stats.writes == updates.load());
    assert(device.takeAsyncWriteError(error) == false);
}

void testApex5WriteSequenceUnchanged() {
    for (const bool async : {false, true}) {
        auto* transport = new RecordingTransport(false);
        Apex5Device device{TransportPtr(transport)};
        std::string error;
        assert(device.verifyIdentity(error));
        const auto policy = device.asyncWritePolicy();
        assert(policy.has_value());
        assert(policy->spacing == milliseconds(10));
        assert(policy->rumbleHoldoffAfterTrigger == milliseconds(30));
        assert(!policy->rumbleStopFirst);
        if (async) assert(device.startAsyncWrites(error));
        const auto baseline = transport->writeCount();
        const asb::ForceTriggerCommand command{TriggerSide::Right, TriggerMode::Race, {25, 40}};
        assert(device.queueTriggerRaw(command, error));
        assert(transport->waitForWrites(baseline + 1));
        assert(device.queueRumble(10, 20, error));
        assert(transport->waitForWrites(baseline + 2));
        assert(device.queueRumble(0, 0, error));
        assert(transport->waitForWrites(baseline + 3));
        device.stopAsyncWrites();

        const auto all = transport->writes();
        assert(all.size() == baseline + 3);
        const auto trigger = asb::flydigi::buildForceTriggerRaw(command, true);
        const auto rumble = asb::flydigi::buildRumble(10, 20);
        const auto stop = asb::flydigi::buildRumble(0, 0);
        assert(all[baseline].bytes == std::vector<std::uint8_t>(trigger.begin(), trigger.end()));
        assert(all[baseline + 1].bytes == std::vector<std::uint8_t>(rumble.begin(), rumble.end()));
        assert(all[baseline + 2].bytes == std::vector<std::uint8_t>(stop.begin(), stop.end()));
        if (async) {
            assert(all[baseline + 1].at - all[baseline].at >= milliseconds(30));
            assert(all[baseline + 2].at - all[baseline + 1].at >= milliseconds(10));
            assert(device.asyncWriteStats().writes == 3);
        } else {
            assert(device.asyncWriteStats().writes == 0);
        }
        assert(device.triggerWriteStats().writes == 1);
        assert(device.rumbleWriteStats().writes == 2);
    }
}

void testReceiverSpacesSynchronousVendorWrites() {
    auto* transport = new RecordingTransport(false);
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    const auto policy = device.asyncWritePolicy();
    assert(policy.has_value() && policy->spacing == milliseconds(10));
    assert(!policy->rumbleStopFirst);
    assert(device.startAsyncWrites(error));
    const auto baseline = transport->writeCount();

    assert(device.setTriggerRaw({TriggerSide::Right, TriggerMode::Race, {25, 40}}, error));
    assert(device.requestBatteryRefresh(error));
    assert(device.queueTriggerRaw({TriggerSide::Left, TriggerMode::Race, {25, 50}}, error));
    assert(transport->waitForWrites(baseline + 3));
    assert(device.setTriggerRaw({TriggerSide::Right, TriggerMode::Race, {25, 60}}, error));
    std::array<std::uint8_t, asb::flydigi::kRgbConfigSize> payload{};
    assert(device.writeRgbConfigRange(0, 0, 1, payload, error));
    assert(device.queueTriggerRaw({TriggerSide::Left, TriggerMode::Race, {25, 70}}, error));
    assert(transport->waitForWrites(baseline + 7));
    device.stopAsyncWrites();

    const auto all = transport->writes();
    assert(all.size() == baseline + 7);
    const std::vector<RecordedWrite> writes(all.begin() + static_cast<std::ptrdiff_t>(baseline),
                                            all.end());
    assert(hasCommand(writes[0], asb::flydigi::kCmdSetForceTrigger));
    assert(hasCommand(writes[1], asb::flydigi::kCmdGetInfo));
    assert(hasCommand(writes[2], asb::flydigi::kCmdSetForceTrigger));
    assert(hasCommand(writes[3], asb::flydigi::kCmdSetForceTrigger));
    assert(hasCommand(writes[4], asb::flydigi::kCmdWriteRgbStart));
    assert(hasCommand(writes[5], asb::flydigi::kCmdWriteRgbPack));
    assert(hasCommand(writes[6], asb::flydigi::kCmdSetForceTrigger));
    assert(writes[1].at - writes[0].at >= policy->spacing);
    assert(writes[2].at - writes[1].at >= policy->spacing);
    assert(writes[4].at - writes[3].at >= policy->spacing);
    assert(writes[6].at - writes[5].at >= policy->spacing);
}

void testMoveCarriesDeviceState() {
    std::string error;
    {
        auto* transport = new RecordingTransport(false, false);
        Apex5Device device{TransportPtr(transport)};
        assert(!device.verifyIdentity(error));
        assert(device.identityUnanswered());
        Apex5Device moved(std::move(device));
        assert(moved.identityUnanswered());
        Apex5Device assigned;
        assigned = std::move(moved);
        assert(assigned.identityUnanswered());
    }

    auto* transport = new RecordingTransport(false);
    Apex5Device device{TransportPtr(transport)};
    assert(device.verifyIdentity(error));
    const auto baseline = transport->writeCount();
    assert(device.setTriggerRaw({TriggerSide::Right, TriggerMode::Race, {25, 40}}, error));
    assert(device.setRumble(10, 20, error));

    Apex5Device moved(std::move(device));
    assert(moved.identity().has_value());
    assert(moved.triggerWriteStats().writes == 1);
    assert(moved.rumbleWriteStats().writes == 1);
    assert(moved.startAsyncWrites(error));
    assert(moved.asyncWritesRunning());
    assert(moved.queueRumble(30, 40, error));
    assert(transport->waitForWrites(baseline + 3));
    moved.stopAsyncWrites();

    const auto writes = transport->writes();
    assert(writes.size() == baseline + 3);
    assert(hasCommand(writes[baseline], asb::flydigi::kCmdSetForceTrigger));
    assert(hasCommand(writes[baseline + 2], asb::flydigi::kCmdSetRumble));
    assert(writes[baseline + 2].at - writes[baseline].at >= milliseconds(30));
    assert(moved.rumbleWriteStats().writes == 2);

    Apex5Device assigned;
    assigned = std::move(moved);
    assert(assigned.identity().has_value());
    assert(assigned.triggerWriteStats().writes == 1);
    assert(assigned.rumbleWriteStats().writes == 2);
}

void testQueueingWhileWriterStartsAndStops() {
    auto* transport = new RecordingTransport(false);
    Apex5Device device{TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    const auto baseline = transport->writeCount();

    std::atomic<bool> feeding{true};
    std::thread feedback([&device, &feeding] {
        std::uint8_t value = 0;
        while (feeding.load(std::memory_order_relaxed)) {
            std::string ignored;
            ++value;
            assert(device.queueTriggerRaw({TriggerSide::Right, TriggerMode::Race, {25, value}},
                                          ignored));
            assert(device.queueRumble(value, 7, ignored));
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        assert(device.startAsyncWrites(error));
        assert(device.asyncWritesRunning());
        std::this_thread::sleep_for(milliseconds(5));
        device.stopAsyncWrites();
        assert(!device.asyncWritesRunning());
    }
    feeding.store(false, std::memory_order_relaxed);
    feedback.join();
    assert(transport->writeCount() > baseline);
    assert(device.takeAsyncWriteError(error) == false);
}

}

int main() {
    testContinuousTriggersDoNotStarveRumble(false);
    testContinuousTriggersDoNotStarveRumble(true);
    testApex4RumbleStopSkipsTriggerQueue();
    testApex4ShortBurstReachesWriter();
    testApex4PacingAndCounters();
    testApex5WriteSequenceUnchanged();
    testReceiverSpacesSynchronousVendorWrites();
    testMoveCarriesDeviceState();
    testQueueingWhileWriterStartsAndStops();
    std::cout << "APEX feedback writer tests passed\n";
    return 0;
}
