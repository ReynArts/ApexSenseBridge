#include "flydigi/Apex5Device.h"

#include "flydigi/Apex4Protocol.h"
#include "flydigi/Apex5Protocol.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <span>
#include <string>
#include <vector>

namespace asb::flydigi {
namespace {

bool isApex4Candidate(const HidDeviceInfo& info) noexcept {
    return isApex4Product(info.vendorId, info.productId) &&
           info.usagePage == kApex4VendorUsagePage &&
           (info.interfaceNumber.empty() || info.interfaceNumber == L"MI_02");
}

std::string hexPreview(std::span<const std::uint8_t> report) {
    std::ostringstream output;
    output << '[' << report.size() << "]";
    for (const auto byte : report) {
        output << ' ' << std::hex << std::uppercase << std::setw(2)
               << std::setfill('0') << static_cast<unsigned int>(byte);
    }
    return output.str();
}

struct Apex4IdentityObservation {
    void record(std::span<const std::uint8_t> report) {
        ++reports;
        if (!report.empty()) ++reportIds[report.front()];

        const bool stateReport = report.size() >= 2 &&
                                 report[0] == kApex4InputReportId &&
                                 report[1] == kApex4StateMarker;
        if (stateReport) ++stateReports;

        const bool containsIdentityCommand =
            std::find(report.begin(), report.end(), kApex4CmdGetInfo) != report.end();
        if ((stateReport && !containsIdentityCommand) || samples.size() >= 4) return;

        const auto preview = hexPreview(report);
        if (std::find(samples.begin(), samples.end(), preview) == samples.end()) {
            samples.push_back(preview);
        }
    }

    [[nodiscard]] std::string describe() const {
        std::ostringstream output;
        output << " observed_reports=" << reports
               << ", state_reports=" << stateReports << ", report_ids=";
        bool first = true;
        for (std::size_t id = 0; id < reportIds.size(); ++id) {
            if (reportIds[id] == 0) continue;
            if (!first) output << '/';
            output << "0x" << std::hex << std::uppercase << std::setw(2)
                   << std::setfill('0') << id << std::dec << ':' << reportIds[id];
            first = false;
        }
        if (first) output << "none";
        if (!samples.empty()) {
            output << ", non_state_or_0xEC_samples=";
            for (std::size_t index = 0; index < samples.size(); ++index) {
                if (index != 0) output << " | ";
                output << samples[index];
            }
        }
        return output.str();
    }

    std::size_t reports = 0;
    std::size_t stateReports = 0;
    std::array<std::size_t, 256> reportIds{};
    std::vector<std::string> samples;
};

} // namespace

void TransportDeleter::operator()(platform::HidTransport* transport) const noexcept {
    platform::destroyHidTransport(transport);
}

Apex5Device::Apex5Device(TransportPtr transport)
    : transport_(std::move(transport)) {}

std::vector<HidDeviceInfo> Apex5Device::findCandidates(std::string& error) {
    auto all = platform::enumerateHidDevices(error);
    std::vector<HidDeviceInfo> candidates;

    std::copy_if(all.begin(), all.end(), std::back_inserter(candidates), [](const HidDeviceInfo& info) {
        const bool apex5 = info.vendorId == kVendorId &&
                           isControllerProduct(info.productId) &&
                           info.usagePage == kVendorUsagePage;
        return apex5 || isApex4Candidate(info);
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

bool Apex5Device::usesApex4Protocol() const noexcept {
    return isOpen() && isApex4Product(
        transport_->info().vendorId, transport_->info().productId);
}

bool Apex5Device::verifyIdentity(std::string& error) {
    if (!isOpen()) {
        error = "APEX device is not open";
        return false;
    }
    if (identity_) {
        return true;
    }
    if (transport_->info().inputReportLength == 0) {
        error = "The candidate HID interface declares no input report for identity replies";
        return false;
    }

    const bool apex4 = usesApex4Protocol();
    const auto protocolInputSize = apex4 ? std::size_t{32} : kReportSize;
    const auto bufferSize = std::max<std::size_t>(
        protocolInputSize, transport_->info().inputReportLength);
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

    // V1 controllers can occasionally miss a successful command write while
    // streaming input, especially over the 2.4 GHz receiver. SDL retries this
    // read-only request up to 30 times; keep the same roughly three-second
    // retry window so a second process opening the dongle is reliable too.
    constexpr auto kApex4AttemptTimeout = std::chrono::milliseconds(100);
    constexpr auto kApex5AttemptTimeout = std::chrono::milliseconds(600);
    constexpr std::size_t kMaximumReplies = 4096;
    const std::size_t maximumAttempts = apex4 ? 30 : 1;
    Apex4IdentityObservation apex4Observation;
    for (std::size_t attempt = 0; attempt < maximumAttempts; ++attempt) {
        const bool requestWritten = apex4
            ? transport_->writeOutputReport(buildApex4IdentityRequest(), error)
            : transport_->writeOutputReport(Apex5Identity::buildRequest(), error);
        if (!requestWritten) {
            error = "Could not send the read-only Flydigi identity request: " + error;
            return false;
        }

        const auto deadline = std::chrono::steady_clock::now() +
            (apex4 ? kApex4AttemptTimeout : kApex5AttemptTimeout);
        for (std::size_t count = 0; count < kMaximumReplies; ++count) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            if (remaining.count() == 0) remaining = std::chrono::milliseconds(1);

            std::size_t bytesRead = 0;
            std::string readError;
            const auto status = transport_->readInputReport(
                input, remaining, bytesRead, readError);
            if (status == platform::HidReadStatus::Timeout) break;
            if (status == platform::HidReadStatus::Error) {
                error = "Could not read the Flydigi identity reply: " + readError;
                return false;
            }

            const auto bytes = std::span<const std::uint8_t>(input.data(), bytesRead);
            if (apex4) apex4Observation.record(bytes);
            const auto parsed = apex4
                ? Apex5Identity::parseApex4Reply(bytes)
                : Apex5Identity::parseReply(bytes);
            if (!parsed) continue;

            const bool expectedModel = apex4 ? parsed->isApex4() : parsed->isApex5();
            if (!expectedModel || !parsed->supportsAdaptiveTriggers()) {
                error = "Identity refused: found " + parsed->describe() +
                        "; adaptive-trigger writes require an Apex 4 (k2) or Apex 5 (k5).";
                return false;
            }
            identity_ = *parsed;
            return true;
        }
    }

    error = apex4
        ? "No valid command 0xEC Apex 4 identity reply arrived after 30 attempts;" +
          apex4Observation.describe() +
          "; use USB/dongle DInput mode and close Flydigi Space Station before retrying"
        : "No valid command 0x01 identity reply arrived within 600 ms; "
          "wake the controller and close Flydigi Space Station before retrying";
    return false;
}

bool Apex5Device::mayWriteEffects(std::string& error) const {
    if (!identity_) {
        error = "Effect write refused: device identity was not verified";
        return false;
    }
    if (!identity_->supportsAdaptiveTriggers()) {
        error = "Effect write refused: " + identity_->describe() +
                " is not a verified Apex 4 or Apex 5";
        return false;
    }
    return true;
}

bool Apex5Device::setTrigger(const TriggerEffect& effect, std::string& error) {
    if (!isOpen()) {
        error = "APEX device is not open";
        return false;
    }
    if (!mayWriteEffects(error)) {
        return false;
    }
    if (identity_->isApex4()) {
        return transport_->writeOutputReport(
            buildApex4ForceTrigger(effect, true), error);
    }
    const auto report = buildForceTrigger(effect, true);
    return transport_->writeOutputReport(report, error);
}

bool Apex5Device::setTriggerRaw(const ForceTriggerCommand& command, std::string& error) {
    if (!isOpen()) {
        error = "APEX device is not open";
        return false;
    }
    if (!mayWriteEffects(error)) {
        return false;
    }
    return identity_->isApex4()
        ? transport_->writeOutputReport(buildApex4ForceTriggerRaw(command, true), error)
        : transport_->writeOutputReport(buildForceTriggerRaw(command, true), error);
}

bool Apex5Device::clearTrigger(TriggerSide side, std::string& error) {
    if (!isOpen()) {
        error = "APEX device is not open";
        return false;
    }
    if (!mayWriteEffects(error)) {
        return false;
    }
    return identity_->isApex4()
        ? transport_->writeOutputReport(buildApex4Normal(side), error)
        : transport_->writeOutputReport(buildNormal(side), error);
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
        error = "APEX device is not open";
        return false;
    }
    if (!mayWriteEffects(error)) {
        return false;
    }
    return identity_->isApex4()
        ? transport_->writeOutputReport(
              buildApex4Rumble(lowFrequencyMotor, highFrequencyMotor), error)
        : transport_->writeOutputReport(
              buildRumble(lowFrequencyMotor, highFrequencyMotor), error);
}

bool Apex5Device::stopRumble(std::string& error) {
    return setRumble(0, 0, error);
}

} // namespace asb::flydigi
