#ifdef NDEBUG
#undef NDEBUG
#endif

#include "flydigi/Apex5Device.h"
#include "flydigi/Apex4Protocol.h"
#include "flydigi/Apex5Identity.h"
#include "flydigi/Apex5Protocol.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

class FakeTransport final : public asb::platform::HidTransport {
public:
    explicit FakeTransport(std::uint8_t deviceType,
                           bool silent = false,
                           bool apex4 = false,
                           std::size_t ignoredApex4Requests = 0)
        : deviceType_(deviceType), silent_(silent), apex4_(apex4),
          ignoredApex4Requests_(ignoredApex4Requests) {
        info_.vendorId = apex4 ? asb::flydigi::kApex4VendorId
                               : asb::flydigi::kVendorId;
        info_.productId = apex4 ? asb::flydigi::kApex4ProductId : 0x2501;
        info_.usagePage = asb::flydigi::kVendorUsagePage;
        info_.interfaceNumber = apex4 ? L"MI_02" : L"MI_01";
        info_.inputReportLength = 32;
        info_.outputReportLength = 32;
    }

    [[nodiscard]] bool isOpen() const noexcept override { return true; }
    [[nodiscard]] const asb::HidDeviceInfo& info() const noexcept override { return info_; }

    bool writeOutputReport(std::span<const std::uint8_t> report, std::string&) override {
        writes.emplace_back(report.begin(), report.end());
        if (!silent_ && apex4_ && report.size() >= 2 &&
            report[0] == asb::flydigi::kApex4CommandReportId &&
            report[1] == asb::flydigi::kApex4CmdGetInfo) {
            if (ignoredApex4Requests_ != 0) {
                --ignoredApex4Requests_;
                return true;
            }
            std::vector<std::uint8_t> reply(32, 0);
            reply[3] = deviceType_;
            reply[9] = 0x34;
            reply[10] = 0x12;
            reply[13] = 1;
            reply[15] = asb::flydigi::kApex4CmdGetInfo;
            replies_.push_back(std::move(reply));
        } else if (!silent_ && !apex4_ && report.size() > 3 &&
                   report[3] == asb::flydigi::kCmdGetInfo) {
            std::vector<std::uint8_t> reply(32, 0);
            reply[0] = asb::flydigi::kReportIdIn;
            reply[1] = asb::flydigi::kMagic0;
            reply[2] = asb::flydigi::kMagic1;
            reply[3] = asb::flydigi::kCmdGetInfo;
            reply[4] = 1;
            reply[6] = deviceType_;
            reply[7] = 2;
            reply[12] = 5;
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
        if (replies_.empty()) {
            return asb::platform::HidReadStatus::Timeout;
        }
        const auto reply = std::move(replies_.front());
        replies_.pop_front();
        const auto count = std::min(report.size(), reply.size());
        std::copy_n(reply.begin(), count, report.begin());
        bytesRead = count;
        return asb::platform::HidReadStatus::Data;
    }

    std::vector<std::vector<std::uint8_t>> writes;

private:
    asb::HidDeviceInfo info_{};
    std::uint8_t deviceType_ = 0;
    bool silent_ = false;
    bool apex4_ = false;
    std::size_t ignoredApex4Requests_ = 0;
    std::deque<std::vector<std::uint8_t>> replies_;
};

asb::flydigi::Apex5Device makeDevice(FakeTransport*& fake,
                                      std::uint8_t deviceType,
                                      bool silent = false,
                                      bool apex4 = false,
                                      std::size_t ignoredApex4Requests = 0) {
    fake = new FakeTransport(deviceType, silent, apex4, ignoredApex4Requests);
    return asb::flydigi::Apex5Device(asb::flydigi::TransportPtr(fake));
}

} // namespace

int main() {
    using namespace asb;
    using namespace asb::flydigi;

    const auto request = Apex5Identity::buildRequest();
    assert(request[0] == 0x03);
    assert(request[1] == 0x5A);
    assert(request[2] == 0xA5);
    assert(request[3] == 0x01);
    assert(request[4] == 2);
    assert(request[5] == 3); // 8-bit sum of command + length.

    std::vector<std::uint8_t> reply(32, 0);
    reply[0] = kReportIdIn;
    reply[1] = kMagic0;
    reply[2] = kMagic1;
    reply[3] = kCmdGetInfo;
    reply[6] = 128;
    reply[7] = 2;
    reply[12] = 5;
    const auto parsed = Apex5Identity::parseReply(reply);
    assert(parsed);
    assert(parsed->isApex5());
    assert(!parsed->isWired());
    assert(parsed->batteryLevel() == 5);

    for (const auto deviceType : {128, 129, 133, 134, 135, 136}) {
        assert(Apex5Identity::isApex5DeviceType(static_cast<std::uint8_t>(deviceType)));
    }
    assert(!Apex5Identity::isApex5DeviceType(130)); // Vader 5 Pro.
    assert(!Apex5Identity::isApex5DeviceType(149)); // Apex 6.

    for (const auto deviceType : {84, 86, 87, 92, 93, 102, 103, 104}) {
        assert(Apex5Identity::isApex4DeviceType(static_cast<std::uint8_t>(deviceType)));
    }
    assert(!Apex5Identity::isApex4DeviceType(85)); // Vader 4 Pro.

    std::vector<std::uint8_t> apex4Reply(32, 0);
    apex4Reply[3] = 84;
    apex4Reply[9] = 0x34;
    apex4Reply[10] = 0x12;
    apex4Reply[13] = 1;
    apex4Reply[15] = kApex4CmdGetInfo;
    const auto parsedApex4 = Apex5Identity::parseApex4Reply(apex4Reply);
    assert(parsedApex4);
    assert(parsedApex4->isApex4());
    assert(!parsedApex4->isApex5());
    assert(parsedApex4->isWired());
    assert(parsedApex4->firmwareVersion() == 0x1234);
    assert(!parsedApex4->hasBatteryLevel());

    FakeTransport* acceptedTransport = nullptr;
    auto accepted = makeDevice(acceptedTransport, 128);
    TriggerEffect effect{};
    std::string error;
    assert(!accepted.setTrigger(effect, error));
    assert(acceptedTransport->writes.empty());
    assert(accepted.verifyIdentity(error));
    assert(accepted.identity() && accepted.identity()->isApex5());
    assert(acceptedTransport->writes.size() == 1);
    assert(acceptedTransport->writes[0][3] == kCmdGetInfo);
    assert(accepted.setTrigger(effect, error));
    assert(acceptedTransport->writes.size() == 2);
    assert(acceptedTransport->writes[1][3] == kCmdSetForceTrigger);

    FakeTransport* apex4Transport = nullptr;
    auto apex4 = makeDevice(apex4Transport, 84, false, true);
    error.clear();
    assert(!apex4.setTrigger(effect, error));
    assert(apex4Transport->writes.empty());
    assert(apex4.verifyIdentity(error));
    assert(apex4.identity() && apex4.identity()->isApex4());
    assert(apex4Transport->writes.size() == 1);
    assert(apex4Transport->writes[0].size() == 12);
    assert(apex4Transport->writes[0][0] == kApex4CommandReportId);
    assert(apex4Transport->writes[0][1] == kApex4CmdGetInfo);
    assert(apex4.setTrigger(effect, error));
    assert(apex4Transport->writes.size() == 2);
    assert(apex4Transport->writes[1].size() == kApex4ForceTriggerReportSize);
    assert(apex4Transport->writes[1][0] == kApex4CommandReportId);
    assert(apex4Transport->writes[1][1] == kApex4CmdSetForceTriggerDInput);

    FakeTransport* intermittentApex4Transport = nullptr;
    auto intermittentApex4 = makeDevice(
        intermittentApex4Transport, 84, false, true, 5);
    error.clear();
    assert(intermittentApex4.verifyIdentity(error));
    assert(intermittentApex4.identity() && intermittentApex4.identity()->isApex4());
    assert(intermittentApex4Transport->writes.size() == 6);

    FakeTransport* wrongTransport = nullptr;
    auto wrong = makeDevice(wrongTransport, 130);
    error.clear();
    assert(!wrong.verifyIdentity(error));
    assert(error.find("Identity refused") != std::string::npos);
    assert(wrongTransport->writes.size() == 1);
    assert(!wrong.setTrigger(effect, error));
    assert(wrongTransport->writes.size() == 1);

    FakeTransport* wrongApex4Transport = nullptr;
    auto wrongApex4 = makeDevice(wrongApex4Transport, 85, false, true);
    error.clear();
    assert(!wrongApex4.verifyIdentity(error));
    assert(error.find("Identity refused") != std::string::npos);
    assert(wrongApex4Transport->writes.size() == 1);

    FakeTransport* silentApex4Transport = nullptr;
    auto silentApex4 = makeDevice(silentApex4Transport, 84, true, true);
    error.clear();
    assert(!silentApex4.verifyIdentity(error));
    assert(error.find("after 30 attempts") != std::string::npos);
    assert(error.find("observed_reports=0") != std::string::npos);
    assert(silentApex4Transport->writes.size() == 30);
    assert(!wrongApex4.setTrigger(effect, error));
    assert(wrongApex4Transport->writes.size() == 1);

    FakeTransport* silentTransport = nullptr;
    auto silent = makeDevice(silentTransport, 128, true);
    error.clear();
    assert(!silent.verifyIdentity(error));
    assert(error.find("No valid command 0x01") != std::string::npos);
    assert(silentTransport->writes.size() == 1);

    std::cout << "Identity guard tests passed\n";
    return 0;
}
