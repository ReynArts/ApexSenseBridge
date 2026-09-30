#ifdef NDEBUG
#undef NDEBUG
#endif

#include "dualsense/AdaptiveTriggerBridge.h"
#include "flydigi/Apex4Protocol.h"
#include "flydigi/Apex5Protocol.h"

#include <algorithm>
#include <cassert>
#include <vector>

namespace {

class FakeTransport final : public asb::platform::HidTransport {
public:
    explicit FakeTransport(bool apex4) : apex4_(apex4) {
        info_.vendorId = apex4 ? asb::flydigi::kApex4VendorId : asb::flydigi::kVendorId;
        info_.productId = apex4 ? asb::flydigi::kApex4ProductId : 0x2501;
        info_.usagePage = asb::flydigi::kVendorUsagePage;
        info_.inputReportLength = 32;
        info_.outputReportLength = 32;
    }

    bool isOpen() const noexcept override { return true; }
    const asb::HidDeviceInfo& info() const noexcept override { return info_; }

    bool writeOutputReport(std::span<const std::uint8_t> report,
                           std::string& error) override {
        const bool identityRequest = apex4_
            ? report.size() >= 2 && report[1] == asb::flydigi::kApex4CmdGetInfo
            : report.size() >= 4 && report[3] == asb::flydigi::kCmdGetInfo;
        if (identityRequest) {
            identityPending_ = true;
            return true;
        }
        if (failWrites) {
            error = "simulated trigger failure";
            return false;
        }
        writes.emplace_back(report.begin(), report.end());
        return true;
    }

    asb::platform::HidReadStatus readInputReport(
        std::span<std::uint8_t> report, std::chrono::milliseconds,
        std::size_t& bytesRead, std::string&) override {
        bytesRead = 0;
        if (!identityPending_) return asb::platform::HidReadStatus::Timeout;
        std::array<std::uint8_t, 32> reply{};
        if (apex4_) {
            reply[3] = 84;
            reply[15] = asb::flydigi::kApex4CmdGetInfo;
        } else {
            reply[0] = asb::flydigi::kReportIdIn;
            reply[1] = asb::flydigi::kMagic0;
            reply[2] = asb::flydigi::kMagic1;
            reply[3] = asb::flydigi::kCmdGetInfo;
            reply[4] = 1;
            reply[6] = 128;
            reply[7] = 2;
        }
        bytesRead = (std::min)(report.size(), reply.size());
        std::copy_n(reply.begin(), bytesRead, report.begin());
        identityPending_ = false;
        return asb::platform::HidReadStatus::Data;
    }

    bool failWrites = false;
    std::vector<std::vector<std::uint8_t>> writes;

private:
    bool apex4_;
    bool identityPending_ = false;
    asb::HidDeviceInfo info_{};
};

void expectReport(const FakeTransport& transport, bool apex4,
                  asb::TriggerSide side, asb::TriggerMode mode,
                  const std::array<std::uint8_t, 5>& params) {
    assert(!transport.writes.empty());
    const auto& report = transport.writes.back();
    const std::size_t offset = apex4 ? 6 : 8;
    if (apex4) {
        assert(report.size() == asb::flydigi::kApex4ForceTriggerReportSize);
        assert(report[0] == asb::flydigi::kApex4CommandReportId);
        assert(report[1] == asb::flydigi::kApex4CmdSetForceTriggerDInput);
        assert(report[2] == asb::flydigi::kApex4ForceTriggerEffectFamily);
        assert(report[3] == 0);
    } else {
        assert(report.size() == asb::flydigi::kReportSize);
        assert(report[0] == asb::flydigi::kReportIdOut);
        assert(report[1] == asb::flydigi::kMagic0);
        assert(report[2] == asb::flydigi::kMagic1);
        assert(report[3] == asb::flydigi::kCmdSetForceTrigger);
        assert(report[4] == 10);
        assert(report[5] == 1);
    }
    assert(report[offset - 2] == static_cast<std::uint8_t>(side));
    assert(report[offset - 1] == static_cast<std::uint8_t>(mode));
    assert(std::equal(params.begin(), params.end(), report.begin() + offset));
}

void testBridge(bool apex4) {
    using namespace asb;
    auto* transport = new FakeTransport(apex4);
    flydigi::Apex5Device device{flydigi::TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    assert(device.identity()->isApex4() == apex4);
    dualsense::AdaptiveTriggerBridge bridge(device);

    dualsense::DualSenseFeedback feedback{};
    feedback.enableBits1 = 0x0C;
    feedback.leftTriggerEffect = {0x26, 0xF0, 0x03, 0, 0, 0, 0, 0, 0, 35};
    feedback.rightTriggerEffect = {0x25, 0x84, 0, 3};
    bridge.handle(feedback);
    assert(transport->writes.size() == 2);
    expectReport(*transport, apex4, TriggerSide::Left, TriggerMode::RecoilRattle,
                 {77, 1, 15, 35, 0});
    const auto initialStats = bridge.stats();
    assert(initialStats.lastRightCommand->mode == TriggerMode::SniperBreak);
    assert((initialStats.lastRightCommand->params == std::array<std::uint8_t, 5>{38, 96, 32, 0, 0}));
    bridge.handle(feedback);
    assert(transport->writes.size() == 2);
    assert(bridge.stats().deduplicated == 2);

    feedback.enableBits1 = 0x01;
    feedback.rumbleLeft = 255;
    feedback.leftTriggerEffect = {5};
    bridge.handle(feedback);
    feedback.enableBits1 = 0x0D;
    feedback.leftTriggerEffect = {0x26, 0xF0, 0x03, 0, 0, 0, 0, 0, 0, 35};
    bridge.handle(feedback);
    assert(transport->writes.size() == 2);

    feedback.enableBits1 = 0x04;
    feedback.leftTriggerEffect = {5};
    feedback.rightTriggerEffect = {5};
    bridge.handle(feedback);
    assert(transport->writes.size() == 3);
    expectReport(*transport, apex4, TriggerSide::Right, TriggerMode::Normal, {});
    assert(bridge.stats().lastLeftCommand->mode == TriggerMode::RecoilRattle);

    feedback.enableBits1 = 0x08;
    feedback.leftTriggerEffect = {0x26, 0xF0, 0x03, 0, 0, 0, 0, 0, 0, 120};
    bridge.handle(feedback);
    assert(transport->writes.size() == 4);
    expectReport(*transport, apex4, TriggerSide::Left, TriggerMode::RecoilRattle,
                 {77, 1, 15, 120, 0});

    feedback.leftTriggerEffect[9] = 0;
    feedback.leftTriggerEffect[10] = 255;
    bridge.handle(feedback);
    assert(transport->writes.size() == 5);
    expectReport(*transport, apex4, TriggerSide::Left, TriggerMode::Normal, {});
    bridge.handle(feedback);
    assert(transport->writes.size() == 5);

    feedback.leftTriggerEffect = {0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 35};
    bridge.handle(feedback);
    assert(transport->writes.size() == 6);
    expectReport(*transport, apex4, TriggerSide::Left, TriggerMode::RecoilRattle,
                 {0, 1, 120, 35, 0});
    feedback.leftTriggerEffect = {5};
    bridge.handle(feedback);
    assert(transport->writes.size() == 7);
    expectReport(*transport, apex4, TriggerSide::Left, TriggerMode::Normal, {});
    feedback.leftTriggerEffect = {0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 35};
    bridge.handle(feedback);
    assert(transport->writes.size() == 8);
    expectReport(*transport, apex4, TriggerSide::Left, TriggerMode::RecoilRattle,
                 {0, 1, 120, 35, 0});
    feedback.leftTriggerEffect = {0x21};
    bridge.handle(feedback);
    assert(transport->writes.size() == 9);
    expectReport(*transport, apex4, TriggerSide::Left, TriggerMode::Normal, {});

    feedback.leftTriggerEffect = {1, 25, 40};
    bridge.handle(feedback);
    assert(transport->writes.size() == 10);
    expectReport(*transport, apex4, TriggerSide::Left, TriggerMode::Race, {25, 40, 0, 0, 0});
    feedback.leftTriggerEffect = {};
    bridge.handle(feedback);
    assert(bridge.stats().neutral == 1);
    feedback.leftTriggerEffect = {0x26, 0xFF, 0x83};
    bridge.handle(feedback);
    feedback.leftTriggerEffect = {99};
    bridge.handle(feedback);
    assert(bridge.stats().unsupported == 2);
    feedback.kind = dualsense::FeedbackKind::AudioHaptics;
    bridge.handle(feedback);
    assert(transport->writes.size() == 10);
    assert(bridge.stats().lastLeftCommand->mode == TriggerMode::Race);

    feedback.kind = dualsense::FeedbackKind::HidOutput;
    feedback.leftTriggerEffect = {5};
    transport->failWrites = true;
    bridge.handle(feedback);
    assert(bridge.failed());
    assert(bridge.error() == "simulated trigger failure");
    assert(bridge.stats().writeFailures == 1);
    transport->failWrites = false;
    bridge.handle(feedback);
    assert(transport->writes.size() == 10);
}

}

void testStrength(bool apex4) {
    using namespace asb;
    auto* transport = new FakeTransport(apex4);
    flydigi::Apex5Device device{flydigi::TransportPtr(transport)};
    std::string error;
    assert(device.verifyIdentity(error));
    dualsense::AdaptiveTriggerBridge bridge(device, 50);
    dualsense::DualSenseFeedback feedback{};
    feedback.enableBits1 = 0x08;
    feedback.leftTriggerEffect = {1, 25, 40};
    bridge.handle(feedback);
    expectReport(*transport, apex4, TriggerSide::Left, TriggerMode::Race, {25, 20, 0, 0, 0});
    feedback.leftTriggerEffect = {2, 25, 90, 60};
    bridge.handle(feedback);
    expectReport(*transport, apex4, TriggerSide::Left, TriggerMode::SniperBreak, {25, 90, 30, 0, 0});
    feedback.leftTriggerEffect = {0x25, 0x84, 0, 3};
    bridge.handle(feedback);
    expectReport(*transport, apex4, TriggerSide::Left, TriggerMode::SniperBreak, {38, 96, 16, 0, 0});
    feedback.leftTriggerEffect = {0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 35};
    bridge.handle(feedback);
    expectReport(*transport, apex4, TriggerSide::Left, TriggerMode::RecoilRattle, {0, 1, 60, 35, 0});
    dualsense::AdaptiveTriggerBridge disabled(device, 0);
    disabled.handle(feedback);
    expectReport(*transport, apex4, TriggerSide::Left, TriggerMode::Normal, {});
}

int main() {
    testBridge(false);
    testBridge(true);
    testStrength(false);
    testStrength(true);
}
