#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/ApexProfileRestoreGuard.h"
#include "flydigi/Apex5Device.h"
#include "flydigi/Apex5Protocol.h"
#include "flydigi/Apex6Protocol.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <deque>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

namespace apex6 = asb::flydigi::apex6;

class FakeApex6Transport final : public asb::platform::HidTransport {
public:
    FakeApex6Transport() {
        info_.vendorId = apex6::kVendorId;
        info_.productId = apex6::kProductId;
        info_.usagePage = apex6::kUsagePage;
        info_.interfaceNumber = L"MI_02";
        info_.inputReportLength = 33;
        info_.outputReportLength = 33;
    }

    [[nodiscard]] bool isOpen() const noexcept override { return true; }
    [[nodiscard]] const asb::HidDeviceInfo& info() const noexcept override { return info_; }

    bool writeOutputReport(std::span<const std::uint8_t> report, std::string&) override {
        writes.emplace_back(report.begin(), report.end());
        if (silent || report.size() != apex6::kReportSize) return true;
        const auto command = report[3];
        if (command == apex6::kCmdGetInfo) {
            std::vector<std::uint8_t> reply(33, 0);
            reply[1] = apex6::kMagic0;
            reply[2] = apex6::kMagic1;
            reply[3] = apex6::kCmdGetInfo;
            reply[4] = 1;
            reply[6] = apex6::kDeviceType;
            reply[7] = 1;
            reply[30] = 0x9F;
            reply[32] = apex6::checksum(std::span<const std::uint8_t>(reply).subspan(1));
            replies_.push_back(std::move(reply));
        } else if (command == apex6::kCmdProfileStatus) {
            for (auto& frame : spontaneousFrames) replies_.push_back(std::move(frame));
            spontaneousFrames.clear();
            replies_.push_back(stateReply(apex6::kCmdProfileStatus, rawSlot));
        } else if (command == apex6::kCmdApplyProfile) {
            const auto requested = report[5];
            if (!ignoreApply) rawSlot = requested;
            replies_.push_back(stateReply(
                apex6::kCmdApplyProfile, staleApplyAck ? previousSlot() : rawSlot));
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

    void queueInput(std::vector<std::uint8_t> frame) { replies_.push_back(std::move(frame)); }

    [[nodiscard]] std::vector<std::uint8_t> stateReply(std::uint8_t command,
                                                       std::uint8_t slot) const {
        std::vector<std::uint8_t> reply(33, 0);
        reply[1] = apex6::kMagic0;
        reply[2] = apex6::kMagic1;
        reply[3] = command;
        reply[4] = errorReplies ? 0 : 1;
        reply[6] = slot;
        for (std::size_t index = 0; index < 4; ++index) {
            reply[7 + index * 2] = static_cast<std::uint8_t>(slotChecksums[index] & 0xFF);
            reply[8 + index * 2] = static_cast<std::uint8_t>(slotChecksums[index] >> 8);
        }
        reply[15] = static_cast<std::uint8_t>(globalChecksum & 0xFF);
        reply[16] = static_cast<std::uint8_t>(globalChecksum >> 8);
        reply[32] = apex6::checksum(std::span<const std::uint8_t>(reply).subspan(1));
        return reply;
    }

    std::vector<std::vector<std::uint8_t>> writes;
    std::vector<std::vector<std::uint8_t>> spontaneousFrames;
    std::array<std::uint16_t, 4> slotChecksums{0x4112, 0x4112, 0x4112, 0x4112};
    std::uint16_t globalChecksum = 0x115D;
    std::uint8_t rawSlot = 0;
    bool silent = false;
    bool errorReplies = false;
    bool ignoreApply = false;
    bool staleApplyAck = false;

private:
    [[nodiscard]] std::uint8_t previousSlot() const noexcept {
        return static_cast<std::uint8_t>((rawSlot + 1) % 4);
    }

    asb::HidDeviceInfo info_{};
    std::deque<std::vector<std::uint8_t>> replies_;
};

asb::flydigi::Apex5Device makeApex6Device(FakeApex6Transport*& fake) {
    fake = new FakeApex6Transport();
    return asb::flydigi::Apex5Device(asb::flydigi::TransportPtr(fake));
}

std::size_t countCommand(const FakeApex6Transport& fake, std::uint8_t command) {
    return static_cast<std::size_t>(std::count_if(
        fake.writes.begin(), fake.writes.end(),
        [command](const std::vector<std::uint8_t>& write) {
            return write.size() > 3 && write[3] == command;
        }));
}

} // namespace

int main() {
    using namespace asb::flydigi;

    std::string error;
    FakeApex6Transport* transport = nullptr;
    auto device = makeApex6Device(transport);

    ProfileStatus status{};
    assert(!device.readProfileStatus(status, error));
    assert(error.find("identity was not verified") != std::string::npos);
    assert(!device.applyProfile(1, error));
    assert(transport->writes.empty());

    assert(device.verifyIdentity(error));
    assert(device.identity() && device.identity()->isApex6());
    assert(device.identity()->supportsProfileSlots());
    assert(transport->writes.size() == 1);

    assert(device.readProfileStatus(status, error));
    assert(status.rawSlot == 0 && status.slot == 0 && !status.switchBank);
    assert(status.checksums);
    assert((status.checksums->slots == std::array<std::uint16_t, 4>{0x4112, 0x4112, 0x4112, 0x4112}));
    assert(status.checksums->global == 0x115D);
    assert(transport->writes.back().size() == apex6::kReportSize);
    assert(transport->writes.back()[0] == apex6::kReportId);
    assert(transport->writes.back()[3] == apex6::kCmdProfileStatus);
    assert(transport->writes.back()[4] == 0x02);
    assert(transport->writes.back()[32] == 0xA3);

    assert(device.applyProfile(0, error));
    assert(transport->writes.back()[3] == apex6::kCmdApplyProfile);
    assert(transport->writes.back()[4] == 0x03);
    assert(transport->writes.back()[5] == 0x00);
    assert(transport->writes.back()[32] == 0xA5);
    assert(device.readProfileStatus(status, error) && status.slot == 0);

    assert(device.applyProfile(2, error));
    assert(transport->writes.back()[5] == 0x02);
    assert(device.readProfileStatus(status, error));
    assert(status.slot == 2 && !status.switchBank);
    assert(status.checksums && status.checksums->global == 0x115D);

    const auto writesBeforeInvalidSlot = transport->writes.size();
    assert(!device.applyProfile(4, error));
    assert(error.find("1..4") != std::string::npos);
    assert(transport->writes.size() == writesBeforeInvalidSlot);

    {
        asb::ApexProfileRestoreGuard restore(device, 2);
        assert(device.applyProfile(3, error));
        assert(device.readProfileStatus(status, error) && status.slot == 3);
    }
    assert(device.readProfileStatus(status, error) && status.slot == 2);

    transport->rawSlot = 5;
    assert(device.readProfileStatus(status, error));
    assert(status.rawSlot == 5 && status.slot == 1 && status.switchBank);
    transport->rawSlot = 8;
    assert(!device.readProfileStatus(status, error));
    assert(error.find("malformed") != std::string::npos);
    transport->rawSlot = 0;

    transport->queueInput(transport->stateReply(apex6::kCmdProfileStatus, 3));
    assert(device.readProfileStatus(status, error));
    assert(status.slot == 0);

    auto unrelated = transport->stateReply(apex6::kCmdGetInfo, 3);
    transport->spontaneousFrames.push_back(std::vector<std::uint8_t>(33, 0));
    transport->spontaneousFrames.push_back(unrelated);
    auto corrupted = transport->stateReply(apex6::kCmdProfileStatus, 3);
    corrupted[32] ^= 0x01;
    transport->spontaneousFrames.push_back(corrupted);
    assert(device.readProfileStatus(status, error));
    assert(status.slot == 0);

    transport->errorReplies = true;
    assert(!device.readProfileStatus(status, error));
    assert(error.find("malformed") != std::string::npos);
    assert(!device.applyProfile(1, error));
    assert(error.find("error or malformed") != std::string::npos);
    transport->errorReplies = false;

    transport->staleApplyAck = true;
    assert(!device.applyProfile(1, error));
    assert(error.find("reported raw slot") != std::string::npos);
    transport->staleApplyAck = false;
    assert(transport->rawSlot == 1);

    transport->silent = true;
    assert(!device.readProfileStatus(status, error));
    assert(error.find("No Flydigi command 0xA1 reply") != std::string::npos);
    assert(!device.applyProfile(0, error));
    assert(error.find("No Flydigi command 0xA2 reply") != std::string::npos);
    transport->silent = false;

    InputTransportStatus inputTransport{};
    const auto writesBeforeRefusals = transport->writes.size();
    assert(!device.readInputTransportStatus(inputTransport, error));
    assert(!device.setInputTransport(true, true, error));
    std::array<std::uint8_t, kRgbConfigSize> rgb{};
    assert(!device.readRgbConfig(0, rgb, error));
    assert(transport->writes.size() == writesBeforeRefusals);

    for (const auto& write : transport->writes) {
        const auto command = write[3];
        assert(command == apex6::kCmdGetInfo || command == apex6::kCmdProfileStatus ||
               command == apex6::kCmdApplyProfile);
    }
    assert(countCommand(*transport, apex6::kCmdGetInfo) == 1);

    std::cout << "apex6 profile slot tests passed\n";
    return 0;
}
