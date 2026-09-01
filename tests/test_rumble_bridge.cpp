#ifdef NDEBUG
#undef NDEBUG
#endif

#include "dualsense/RumbleBridge.h"
#include "flydigi/Apex5Identity.h"
#include "flydigi/Apex5Protocol.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace {

class FakeTransport final : public asb::platform::HidTransport {
public:
    FakeTransport() {
        info_.vendorId = asb::flydigi::kVendorId;
        info_.productId = 0x2501;
        info_.usagePage = asb::flydigi::kVendorUsagePage;
        info_.inputReportLength = 32;
        info_.outputReportLength = 32;
    }

    [[nodiscard]] bool isOpen() const noexcept override { return true; }
    [[nodiscard]] const asb::HidDeviceInfo& info() const noexcept override { return info_; }

    bool writeOutputReport(std::span<const std::uint8_t> report,
                           std::string& error) override {
        if (failWrites && report.size() > 3 &&
            report[3] == asb::flydigi::kCmdSetRumble) {
            error = "simulated rumble failure";
            return false;
        }
        writes.emplace_back(report.begin(), report.end());
        if (report.size() > 3 && report[3] == asb::flydigi::kCmdGetInfo) {
            std::vector<std::uint8_t> reply(32, 0);
            reply[0] = asb::flydigi::kReportIdIn;
            reply[1] = asb::flydigi::kMagic0;
            reply[2] = asb::flydigi::kMagic1;
            reply[3] = asb::flydigi::kCmdGetInfo;
            reply[4] = 1;
            reply[6] = 128;
            reply[7] = 2;
            replies_.push_back(std::move(reply));
        }
        return true;
    }

    asb::platform::HidReadStatus readInputReport(
        std::span<std::uint8_t> report,
        std::chrono::milliseconds,
        std::size_t& bytesRead,
        std::string&) override {
        bytesRead = 0;
        if (replies_.empty()) return asb::platform::HidReadStatus::Timeout;
        const auto reply = std::move(replies_.front());
        replies_.pop_front();
        const auto count = std::min(report.size(), reply.size());
        std::copy_n(reply.begin(), count, report.begin());
        bytesRead = count;
        return asb::platform::HidReadStatus::Data;
    }

    bool failWrites = false;
    std::vector<std::vector<std::uint8_t>> writes;

private:
    asb::HidDeviceInfo info_{};
    std::deque<std::vector<std::uint8_t>> replies_;
};

} // namespace

int main() {
    using namespace asb::dualsense;

    auto* transport = new FakeTransport();
    asb::flydigi::Apex5Device device{asb::flydigi::TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    assert(transport->writes.size() == 1);

    RumbleBridge bridge(device);
    DualSenseFeedback feedback{};
    feedback.enableBits1 = 0x01;
    feedback.rumbleLeft = 42;
    feedback.rumbleRight = 17;
    bridge.handle(feedback);
    assert(transport->writes.size() == 2);
    assert(transport->writes.back()[3] == asb::flydigi::kCmdSetRumble);
    assert(transport->writes.back()[5] == 42);
    assert(transport->writes.back()[6] == 17);

    bridge.handle(feedback);
    assert(transport->writes.size() == 2);

    feedback.enableBits1 = 0;
    feedback.rumbleLeft = 99;
    bridge.handle(feedback);
    assert(transport->writes.size() == 2);

    feedback.enableBits1 = 0x01;
    feedback.rumbleLeft = 0;
    feedback.rumbleRight = 0;
    bridge.handle(feedback);
    assert(transport->writes.size() == 3);
    assert(transport->writes.back()[5] == 0);
    assert(transport->writes.back()[6] == 0);

    const auto stats = bridge.stats();
    assert(stats.updates == 3);
    assert(stats.writes == 2);
    assert(stats.stops == 1);
    assert(stats.deduplicated == 1);
    assert(stats.writeFailures == 0);
    assert(stats.lastLowFrequency == 0);
    assert(stats.lastHighFrequency == 0);

    DualSenseFeedback audio{};
    audio.kind = FeedbackKind::AudioHaptics;
    audio.leftEnergy = 65535;
    audio.leftPeak = 65535;
    audio.leftTransient = 65535;
    bridge.handle(audio);
    assert(transport->writes.size() == 4);
    assert(transport->writes.back()[5] == 217);
    assert(transport->writes.back()[6] == 0);

    bridge.handle(audio);
    assert(transport->writes.size() == 4);

    audio.leftEnergy = 0;
    audio.leftPeak = 0;
    audio.leftTransient = 0;
    bridge.handle(audio);
    assert(transport->writes.size() == 5);
    assert(transport->writes.back()[5] == 0);
    assert(transport->writes.back()[6] == 0);

    const auto audioStats = bridge.stats();
    assert(audioStats.audioFrames == 3);
    assert(audioStats.audioActiveFrames == 2);
    assert(audioStats.audioHighFrames == 2);
    assert(audioStats.maximumLeftEnergy == 65535);
    assert(audioStats.maximumLeftPeak == 65535);
    assert(audioStats.maximumLeftTransient == 65535);
    assert(audioStats.lastAudioLowFrequency == 0);
    assert(audioStats.writes == 4);
    assert(audioStats.stops == 2);

    transport->failWrites = true;
    feedback.rumbleLeft = 1;
    bridge.handle(feedback);
    assert(bridge.failed());
    assert(bridge.error() == "simulated rumble failure");
    assert(bridge.stats().writeFailures == 1);
    return 0;
}
