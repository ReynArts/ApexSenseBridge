#include "platform/VendorPhysicalInputSource.h"

#include "flydigi/Apex4Input.h"
#include "flydigi/Apex4Protocol.h"
#include "flydigi/Apex5Input.h"
#include "flydigi/Apex5Protocol.h"
#include "platform/HidTransport.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace asb::platform {
namespace {

using Decoder = std::optional<dualsense::DualSenseInputState> (*)(
    std::span<const std::uint8_t>) noexcept;

struct TransportDeleter {
    void operator()(HidTransport* transport) const noexcept {
        destroyHidTransport(transport);
    }
};

class VendorPhysicalInputSource final : public PhysicalInputSource {
public:
    VendorPhysicalInputSource(std::unique_ptr<HidTransport, TransportDeleter> transport,
                              Decoder decode,
                              const char* backendName,
                              std::size_t reportLength)
        : transport_(std::move(transport)),
          decode_(decode),
          backendName_(backendName),
          report_(reportLength, 0) {}

    PhysicalInputStatus waitForState(dualsense::DualSenseInputState& state,
                                     std::chrono::milliseconds timeout,
                                     std::string& error) override {
        error.clear();
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining <= std::chrono::milliseconds::zero()) {
                ++stats_.timeouts;
                return PhysicalInputStatus::Timeout;
            }

            std::size_t bytesRead = 0;
            const auto status = transport_->readInputReport(report_, remaining,
                                                            bytesRead, error);
            if (status == HidReadStatus::Timeout) {
                ++stats_.timeouts;
                return PhysicalInputStatus::Timeout;
            }
            if (status == HidReadStatus::Error) {
                // A vanished device node means the pad was unplugged rather
                // than the read genuinely failing.
                std::error_code ignored;
                if (!std::filesystem::exists(transport_->info().path, ignored)) {
                    error = "The physical APEX input stream disconnected.";
                    return PhysicalInputStatus::Disconnected;
                }
                return PhysicalInputStatus::Error;
            }

            ++stats_.reports;
            const auto decoded = decode_(std::span<const std::uint8_t>(
                report_.data(), bytesRead));
            if (!decoded) {
                // Identity replies and other vendor notifications share this
                // stream. They are valid traffic, just not controller state.
                continue;
            }
            state = *decoded;
            return PhysicalInputStatus::State;
        }
    }

    [[nodiscard]] std::string_view backendName() const noexcept override {
        return backendName_;
    }
    [[nodiscard]] bool eventDriven() const noexcept override { return true; }
    [[nodiscard]] PhysicalInputSourceStats stats() const noexcept override {
        return stats_;
    }

private:
    std::unique_ptr<HidTransport, TransportDeleter> transport_;
    Decoder decode_;
    const char* backendName_;
    std::vector<std::uint8_t> report_;
    PhysicalInputSourceStats stats_{};
};

std::unique_ptr<PhysicalInputSource> openVendorSource(
    const HidDeviceInfo& vendorInterface,
    Decoder decode,
    const char* backendName,
    std::string& error) {
    std::string openError;
    std::unique_ptr<HidTransport, TransportDeleter> transport(
        createHidTransport(vendorInterface, openError));
    if (!transport) {
        error = std::move(openError);
        return {};
    }
    return std::make_unique<VendorPhysicalInputSource>(
        std::move(transport), decode, backendName,
        vendorInterface.inputReportLength);
}

} // namespace

std::unique_ptr<PhysicalInputSource> openApex4VendorInputSource(
    const HidDeviceInfo& vendorInterface, std::string& error) {
    if (!flydigi::isApex4Product(vendorInterface.vendorId, vendorInterface.productId) ||
        vendorInterface.usagePage != flydigi::kApex4VendorUsagePage ||
        vendorInterface.inputReportLength < 32) {
        error = "The selected interface is not a complete Apex 4 V1 vendor interface.";
        return {};
    }
    return openVendorSource(vendorInterface, &flydigi::decodeApex4InputReport,
                            "apex4-v1-hid-event", error);
}

std::unique_ptr<PhysicalInputSource> openApex5VendorInputSource(
    const HidDeviceInfo& vendorInterface, std::string& error) {
    if (vendorInterface.vendorId != flydigi::kVendorId ||
        !flydigi::isControllerProduct(vendorInterface.productId) ||
        vendorInterface.usagePage != flydigi::kVendorUsagePage ||
        vendorInterface.inputReportLength < 18) {
        error = "The selected interface is not a complete Apex 5 vendor input stream.";
        return {};
    }
    return openVendorSource(vendorInterface, &flydigi::decodeApex5InputReport,
                            "apex5-v2-hid-event", error);
}

} // namespace asb::platform
