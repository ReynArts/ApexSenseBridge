#include "flydigi/Apex5Device.h"

#include "flydigi/Apex5Protocol.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <vector>

namespace asb::flydigi {

void TransportDeleter::operator()(platform::HidTransport* transport) const noexcept {
    platform::destroyHidTransport(transport);
}

Apex5Device::Apex5Device(TransportPtr transport)
    : transport_(std::move(transport)) {}

std::vector<HidDeviceInfo> Apex5Device::findCandidates(std::string& error) {
    auto all = platform::enumerateHidDevices(error);
    std::vector<HidDeviceInfo> candidates;

    std::copy_if(all.begin(), all.end(), std::back_inserter(candidates), [](const HidDeviceInfo& info) {
        return info.vendorId == kVendorId &&
               isControllerProduct(info.productId) &&
               info.usagePage == kVendorUsagePage;
    });

    return candidates;
}

std::optional<Apex5Device> Apex5Device::open(const HidDeviceInfo& info, std::string& error) {
    TransportPtr transport(platform::createHidTransport(info, error));
    if (!transport || !transport->isOpen()) {
        return std::nullopt;
    }
    return Apex5Device(std::move(transport));
}

bool Apex5Device::isOpen() const noexcept {
    return transport_ && transport_->isOpen();
}

const HidDeviceInfo& Apex5Device::info() const {
    return transport_->info();
}

const std::optional<Apex5Identity>& Apex5Device::identity() const noexcept {
    return identity_;
}

bool Apex5Device::verifyIdentity(std::string& error) {
    if (!isOpen()) {
        error = "Apex 5 device is not open";
        return false;
    }
    if (identity_) {
        return true;
    }
    if (transport_->info().inputReportLength == 0) {
        error = "The candidate HID interface declares no input report for identity replies";
        return false;
    }

    const auto bufferSize = std::max<std::size_t>(
        kReportSize, transport_->info().inputReportLength);
    std::vector<std::uint8_t> input(bufferSize, 0);

    // Discard a bounded amount of input queued before this exchange. Motion
    // reports can arrive continuously, so the bound prevents a live stream
    // from postponing the identity request forever.
    constexpr std::size_t kMaximumDrainReports = 128;
    for (std::size_t count = 0; count < kMaximumDrainReports; ++count) {
        std::size_t bytesRead = 0;
        std::string readError;
        const auto status = transport_->readInputReport(
            input, std::chrono::milliseconds(0), bytesRead, readError);
        if (status == platform::HidReadStatus::Timeout) {
            break;
        }
        if (status == platform::HidReadStatus::Error) {
            error = "Could not drain stale HID input before identity read: " + readError;
            return false;
        }
    }

    const auto request = Apex5Identity::buildRequest();
    if (!transport_->writeOutputReport(request, error)) {
        error = "Could not send the read-only Flydigi identity request: " + error;
        return false;
    }

    constexpr auto kIdentityTimeout = std::chrono::milliseconds(600);
    constexpr std::size_t kMaximumReplies = 4096;
    const auto deadline = std::chrono::steady_clock::now() + kIdentityTimeout;
    for (std::size_t count = 0; count < kMaximumReplies; ++count) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            break;
        }
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        if (remaining.count() == 0) {
            remaining = std::chrono::milliseconds(1);
        }

        std::size_t bytesRead = 0;
        std::string readError;
        const auto status = transport_->readInputReport(input, remaining, bytesRead, readError);
        if (status == platform::HidReadStatus::Timeout) {
            break;
        }
        if (status == platform::HidReadStatus::Error) {
            error = "Could not read the Flydigi identity reply: " + readError;
            return false;
        }

        const auto parsed = Apex5Identity::parseReply(
            std::span<const std::uint8_t>(input.data(), bytesRead));
        if (!parsed) {
            continue;
        }
        if (!parsed->isApex5() || !parsed->supportsAdaptiveTriggers()) {
            error = "Identity refused: found " + parsed->describe() +
                    "; adaptive-trigger writes require an Apex 5 (k5).";
            return false;
        }
        identity_ = *parsed;
        return true;
    }

    error = "No valid command 0x01 identity reply arrived within 600 ms; "
            "wake the controller and close Flydigi Space Station before retrying";
    return false;
}

bool Apex5Device::mayWriteEffects(std::string& error) const {
    if (!identity_) {
        error = "Effect write refused: device identity was not verified";
        return false;
    }
    if (!identity_->isApex5() || !identity_->supportsAdaptiveTriggers()) {
        error = "Effect write refused: " + identity_->describe() +
                " is not a verified Apex 5";
        return false;
    }
    return true;
}

bool Apex5Device::setTrigger(const TriggerEffect& effect, std::string& error) {
    if (!isOpen()) {
        error = "Apex 5 device is not open";
        return false;
    }
    if (!mayWriteEffects(error)) {
        return false;
    }
    const auto report = buildForceTrigger(effect, true);
    return transport_->writeOutputReport(report, error);
}

bool Apex5Device::setTriggerRaw(const ForceTriggerCommand& command, std::string& error) {
    if (!isOpen()) {
        error = "Apex 5 device is not open";
        return false;
    }
    if (!mayWriteEffects(error)) {
        return false;
    }
    return transport_->writeOutputReport(buildForceTriggerRaw(command, true), error);
}

bool Apex5Device::clearTrigger(TriggerSide side, std::string& error) {
    if (!isOpen()) {
        error = "Apex 5 device is not open";
        return false;
    }
    if (!mayWriteEffects(error)) {
        return false;
    }
    const auto report = buildNormal(side);
    return transport_->writeOutputReport(report, error);
}

bool Apex5Device::clearAll(std::string& error) {
    if (!mayWriteEffects(error)) {
        return false;
    }
    std::string leftError;
    std::string rightError;
    const bool leftOk = clearTrigger(TriggerSide::Left, leftError);
    const bool rightOk = clearTrigger(TriggerSide::Right, rightError);

    if (!leftOk || !rightOk) {
        error = "Failed to clear triggers:";
        if (!leftOk) {
            error += " LT=" + leftError;
        }
        if (!rightOk) {
            error += " RT=" + rightError;
        }
        return false;
    }
    return true;
}

bool Apex5Device::setRumble(std::uint8_t lowFrequencyMotor,
                            std::uint8_t highFrequencyMotor,
                            std::string& error) {
    if (!isOpen()) {
        error = "Apex 5 device is not open";
        return false;
    }
    if (!mayWriteEffects(error)) {
        return false;
    }
    return transport_->writeOutputReport(
        buildRumble(lowFrequencyMotor, highFrequencyMotor), error);
}

bool Apex5Device::stopRumble(std::string& error) {
    return setRumble(0, 0, error);
}

} // namespace asb::flydigi
