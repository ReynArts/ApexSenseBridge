#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/ApexMotionDiagnosticGuard.h"
#include "flydigi/Apex5Device.h"
#include "flydigi/Apex6Imu.h"
#include "flydigi/Apex6Protocol.h"
#include "platform/HidTransport.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

namespace apex6 = asb::flydigi::apex6;

constexpr std::array<std::uint8_t, 33> kHardwareInfoReply{
    0x00, 0x5A, 0xA5, 0x01, 0x01, 0x00, 0x96, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x08, 0x45, 0x01, 0x00, 0x90, 0x50, 0x05, 0x09, 0x01, 0x09,
    0x00, 0x00, 0x01, 0x45, 0x00, 0x00, 0x10, 0x50, 0x9F, 0x00, 0x24};

std::vector<std::uint8_t> sealed(std::vector<std::uint8_t> report) {
    report[32] = apex6::checksum(std::span<const std::uint8_t>(report).subspan(1));
    return report;
}

std::vector<std::uint8_t> operatorFrame() {
    std::vector<std::uint8_t> report(apex6::kReportSize, 0);
    report[1] = apex6::kMagic0;
    report[2] = apex6::kMagic1;
    report[3] = apex6::kCmdOperatorData;
    report[24 + 4] = 0x00;
    report[24 + 5] = 0x10;
    return sealed(std::move(report));
}

class FakeApex6Transport final : public asb::platform::HidTransport {
public:
    FakeApex6Transport() {
        info_.vendorId = apex6::kVendorId;
        info_.productId = apex6::kProductId;
        info_.usagePage = apex6::kUsagePage;
        info_.inputReportLength = 33;
        info_.outputReportLength = 33;
    }

    [[nodiscard]] bool isOpen() const noexcept override { return true; }
    [[nodiscard]] const asb::HidDeviceInfo& info() const noexcept override { return info_; }

    bool writeOutputReport(std::span<const std::uint8_t> report, std::string&) override {
        writes.emplace_back(report.begin(), report.end());
        if (report.size() != 33) return true;
        if (report[3] == apex6::kCmdGetInfo) {
            std::vector<std::uint8_t> reply(kHardwareInfoReply.begin(), kHardwareInfoReply.end());
            reply[30] = features;
            replies_.push_back(sealed(std::move(reply)));
        } else if (report[3] == apex6::kCmdReadTransport) {
            std::vector<std::uint8_t> reply(33, 0);
            reply[1] = apex6::kMagic0;
            reply[2] = apex6::kMagic1;
            reply[3] = apex6::kCmdReadTransport;
            reply[4] = 1;
            reply[6] = takeover ? 1 : 0;
            replies_.push_back(sealed(std::move(reply)));
        } else if (report[3] == apex6::kCmdSetTransport) {
            raw_ = report[6] == 1;
        }
        return true;
    }

    asb::platform::HidReadStatus readInputReport(
        std::span<std::uint8_t> report,
        std::chrono::milliseconds,
        std::size_t& bytesRead,
        std::string&) override {
        bytesRead = 0;
        std::vector<std::uint8_t> next;
        if (!replies_.empty()) {
            next = std::move(replies_.front());
            replies_.pop_front();
        } else if (streamAlways || (raw_ && streamOnRaw)) {
            next = operatorFrame();
        } else {
            return asb::platform::HidReadStatus::Timeout;
        }
        const auto count = std::min(report.size(), next.size());
        std::copy_n(next.begin(), count, report.begin());
        bytesRead = count;
        return asb::platform::HidReadStatus::Data;
    }

    void queueStreamBacklog(std::size_t frames) {
        for (std::size_t index = 0; index < frames; ++index) replies_.push_back(operatorFrame());
    }

    [[nodiscard]] std::size_t transportWrites(bool enable) const {
        std::size_t count = 0;
        for (const auto& write : writes) {
            if (write.size() == 33 && write[3] == apex6::kCmdSetTransport &&
                write[6] == (enable ? 1 : 0)) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] bool rawEnabled() const noexcept { return raw_; }

    std::vector<std::vector<std::uint8_t>> writes;
    std::uint8_t features = 0x9F;
    bool takeover = false;
    bool streamAlways = false;
    bool streamOnRaw = true;

private:
    asb::HidDeviceInfo info_{};
    std::deque<std::vector<std::uint8_t>> replies_;
    bool raw_ = false;
};

asb::flydigi::Apex5Device makeDevice(FakeApex6Transport*& fake) {
    fake = new FakeApex6Transport();
    return asb::flydigi::Apex5Device(asb::flydigi::TransportPtr(fake));
}

} // namespace

int main() {
    std::string error;

    {
        FakeApex6Transport* fake = nullptr;
        auto device = makeDevice(fake);
        assert(device.verifyIdentity(error));
        assert(device.identity() && device.identity()->isApex6());

        apex6::TransportStatus status{};
        assert(device.readApex6TransportStatus(status, error));
        assert(!status.takeoverActive());

        {
            asb::ApexMotionDiagnosticGuard guard(device);
            assert(guard.enable(error));
            assert(fake->rawEnabled());
            assert(fake->transportWrites(true) == 1);
            assert(fake->transportWrites(false) == 0);
        }
        assert(!fake->rawEnabled());
        assert(fake->transportWrites(false) == 1);
    }

    {
        FakeApex6Transport* fake = nullptr;
        auto device = makeDevice(fake);
        assert(device.verifyIdentity(error));
        fake->streamAlways = true;
        {
            asb::ApexMotionDiagnosticGuard guard(device);
            assert(guard.enable(error));
            std::string ignored;
            assert(guard.restore(ignored));
        }
        assert(fake->transportWrites(true) == 0);
        assert(fake->transportWrites(false) == 0);
    }

    {
        FakeApex6Transport* fake = nullptr;
        auto device = makeDevice(fake);
        assert(device.verifyIdentity(error));
        fake->takeover = true;
        error.clear();
        {
            asb::ApexMotionDiagnosticGuard guard(device);
            assert(!guard.enable(error));
            assert(error.find("owns") != std::string::npos);
        }
        assert(fake->transportWrites(true) == 0);
        assert(fake->transportWrites(false) == 0);
    }

    {
        FakeApex6Transport* fake = nullptr;
        auto device = makeDevice(fake);
        assert(device.verifyIdentity(error));
        fake->streamOnRaw = false;
        error.clear();
        {
            asb::ApexMotionDiagnosticGuard guard(device);
            assert(!guard.enable(error));
            assert(error.find("no") != std::string::npos);
            assert(fake->transportWrites(true) == 1);
        }
        assert(fake->transportWrites(false) == 1);
    }

    {
        FakeApex6Transport* fake = nullptr;
        auto device = makeDevice(fake);
        fake->features = 0x9E;
        assert(device.verifyIdentity(error));
        error.clear();
        {
            asb::ApexMotionDiagnosticGuard guard(device);
            assert(!guard.enable(error));
            assert(error.find("IMU") != std::string::npos);
        }
        assert(fake->transportWrites(true) == 0);
        assert(fake->transportWrites(false) == 0);
        assert(!device.setApex6RawDataRouting(true, error));
    }

    {
        FakeApex6Transport* fake = nullptr;
        auto device = makeDevice(fake);
        assert(device.verifyIdentity(error));
        fake->streamAlways = true;
        fake->queueStreamBacklog(200);
        assert(device.requestApex6Info(error));
        std::optional<asb::flydigi::Apex5Identity> reading;
        auto outcome = asb::flydigi::Apex6InfoPoll::Pending;
        for (int poll = 0; poll < 4 && outcome == asb::flydigi::Apex6InfoPoll::Pending; ++poll) {
            outcome = device.pollApex6Info(8, reading, error);
        }
        assert(outcome == asb::flydigi::Apex6InfoPoll::Reply);
        assert(reading && reading->isApex6());
    }

    return 0;
}
