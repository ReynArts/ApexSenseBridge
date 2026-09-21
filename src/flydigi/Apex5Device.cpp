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
#include <thread>
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

std::optional<std::vector<std::uint8_t>> exchangeCommand(
    platform::HidTransport& transport,
    const Report& request,
    std::uint8_t command,
    std::string& error) {
    const auto bufferSize = std::max<std::size_t>(
        kReportSize, transport.info().inputReportLength);
    std::vector<std::uint8_t> input(bufferSize, 0);

    constexpr std::size_t kMaximumDrainReports = 128;
    for (std::size_t count = 0; count < kMaximumDrainReports; ++count) {
        std::size_t bytesRead = 0;
        std::string readError;
        const auto status = transport.readInputReport(
            input, std::chrono::milliseconds(0), bytesRead, readError);
        if (status == platform::HidReadStatus::Timeout) break;
        if (status == platform::HidReadStatus::Error) {
            error = "Could not drain stale HID input before Flydigi command: " +
                    readError;
            return std::nullopt;
        }
    }

    std::string writeError;
    if (!transport.writeOutputReport(request, writeError)) {
        error = "Could not send Flydigi command 0x";
        std::ostringstream commandText;
        commandText << std::hex << std::uppercase
                    << static_cast<unsigned int>(command);
        error += commandText.str() + ": " + writeError;
        return std::nullopt;
    }

    constexpr auto kReplyTimeout = std::chrono::milliseconds(750);
    constexpr std::size_t kMaximumReplies = 4096;
    const auto deadline = std::chrono::steady_clock::now() + kReplyTimeout;
    for (std::size_t count = 0; count < kMaximumReplies; ++count) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now);
        if (remaining.count() == 0) remaining = std::chrono::milliseconds(1);

        std::size_t bytesRead = 0;
        std::string readError;
        const auto status = transport.readInputReport(
            input, remaining, bytesRead, readError);
        if (status == platform::HidReadStatus::Timeout) break;
        if (status == platform::HidReadStatus::Error) {
            error = "Could not read the Flydigi command reply: " + readError;
            return std::nullopt;
        }
        const auto bytes = std::span<const std::uint8_t>(input.data(), bytesRead);
        if (isProfileCommandReply(bytes, command)) {
            return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
        }
    }

    std::ostringstream commandText;
    commandText << std::hex << std::uppercase
                << static_cast<unsigned int>(command);
    error = "No Flydigi command 0x" + commandText.str() +
            " reply arrived within 750 ms; wake the controller and close "
            "Flydigi Space Station before retrying";
    return std::nullopt;
}

} // namespace

void TransportDeleter::operator()(platform::HidTransport* transport) const noexcept {
    platform::destroyHidTransport(transport);
}

Apex5Device::Apex5Device(TransportPtr transport)
    : transport_(std::move(transport)) {}

Apex5Device::~Apex5Device() {
    stopAsyncWrites();
}

Apex5Device::Apex5Device(Apex5Device&& other) noexcept {
    other.stopAsyncWrites();
    transport_ = std::move(other.transport_);
    identity_ = std::move(other.identity_);
    writeMutex_ = std::move(other.writeMutex_);
    if (!writeMutex_) writeMutex_ = std::make_unique<std::recursive_mutex>();
    lastVendorWriteAt_ = other.lastVendorWriteAt_;
}

Apex5Device& Apex5Device::operator=(Apex5Device&& other) noexcept {
    if (this == &other) return *this;
    stopAsyncWrites();
    other.stopAsyncWrites();
    transport_ = std::move(other.transport_);
    identity_ = std::move(other.identity_);
    writeMutex_ = std::move(other.writeMutex_);
    if (!writeMutex_) writeMutex_ = std::make_unique<std::recursive_mutex>();
    lastVendorWriteAt_ = other.lastVendorWriteAt_;
    return *this;
}

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

bool Apex5Device::mayControlProfiles(std::string& error) const {
    if (!isOpen()) {
        error = "APEX device is not open";
        return false;
    }
    if (!identity_) {
        error = "Profile command refused: device identity was not verified";
        return false;
    }
    if (!identity_->isApex5()) {
        error = "Profile command refused: this diagnostic currently supports only "
                "a verified Apex 5";
        return false;
    }
    return true;
}

std::unique_lock<std::recursive_mutex> Apex5Device::acquireWriteLock() const noexcept {
    return writeMutex_ ? std::unique_lock(*writeMutex_) : std::unique_lock<std::recursive_mutex>();
}

bool Apex5Device::writeSpacedOutputReport(
    std::span<const std::uint8_t> report, std::string& error) {
    constexpr auto kVendorWriteSpacing = std::chrono::milliseconds(25);
    const auto lock = acquireWriteLock();
    if (lastVendorWriteAt_.time_since_epoch().count() != 0) {
        const auto since = std::chrono::steady_clock::now() - lastVendorWriteAt_;
        if (since < kVendorWriteSpacing) {
            std::this_thread::sleep_for(kVendorWriteSpacing - since);
        }
    }
    const bool ok = transport_->writeOutputReport(report, error);
    lastVendorWriteAt_ = std::chrono::steady_clock::now();
    return ok;
}

bool Apex5Device::setTrigger(const TriggerEffect& effect, std::string& error) {
    if (!isOpen()) {
        error = "APEX device is not open";
        return false;
    }
    if (!mayWriteEffects(error)) {
        return false;
    }
    const auto lock = acquireWriteLock();
    if (identity_->isApex4()) {
        return writeSpacedOutputReport(buildApex4ForceTrigger(effect), error);
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
    const auto lock = acquireWriteLock();
    return identity_->isApex4()
        ? writeSpacedOutputReport(buildApex4ForceTriggerRaw(command), error)
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
    const auto lock = acquireWriteLock();
    return identity_->isApex4()
        ? writeSpacedOutputReport(buildApex4Normal(side), error)
        : transport_->writeOutputReport(buildNormal(side), error);
}

bool Apex5Device::clearAll(std::string& error) {
    if (!mayWriteEffects(error)) {
        return false;
    }
    const auto lock = acquireWriteLock();
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
    const auto lock = acquireWriteLock();
    return identity_->isApex4()
        ? writeSpacedOutputReport(
              buildApex4Rumble(lowFrequencyMotor, highFrequencyMotor), error)
        : transport_->writeOutputReport(
              buildRumble(lowFrequencyMotor, highFrequencyMotor), error);
}

bool Apex5Device::stopRumble(std::string& error) {
    return setRumble(0, 0, error);
}

bool Apex5Device::readProfileStatus(ProfileStatus& status, std::string& error) {
    if (!mayControlProfiles(error)) return false;
    const auto reply = exchangeCommand(
        *transport_, buildProfileStatusRequest(), kCmdProfileStatus, error);
    if (!reply) return false;
    const auto parsed = parseProfileStatus(*reply);
    if (!parsed) {
        error = "The Apex 5 returned a malformed or unknown profile status";
        return false;
    }
    status = *parsed;
    return true;
}

bool Apex5Device::applyProfile(std::uint8_t slot, std::string& error) {
    if (!mayControlProfiles(error)) return false;
    const auto request = buildApplyProfile(slot);
    if (!request) {
        error = "Profile slot must be in the range 1..4";
        return false;
    }
    return exchangeCommand(
        *transport_, *request, kCmdApplyProfile, error).has_value();
}

bool Apex5Device::readInputTransportStatus(InputTransportStatus& status,
                                           std::string& error) {
    if (!mayControlProfiles(error)) return false;
    const auto reply = exchangeCommand(
        *transport_, buildInputTransportStatusRequest(),
        kCmdReadInputTransport, error);
    if (!reply) return false;
    const auto parsed = parseInputTransportStatus(*reply);
    if (!parsed) {
        error = "The Apex 5 returned a malformed input-transport status";
        return false;
    }
    status = *parsed;
    return true;
}

bool Apex5Device::setInputTransport(bool controllerData, bool rawData,
                                    std::string& error) {
    if (!mayControlProfiles(error)) return false;
    if (!exchangeCommand(
            *transport_, buildSetInputTransport(controllerData, rawData),
            kCmdSetInputTransport, error)) {
        return false;
    }

    InputTransportStatus effective{};
    if (!readInputTransportStatus(effective, error)) return false;
    if (effective.controllerData != controllerData ||
        effective.rawData != rawData) {
        error = "The Apex 5 did not retain the requested physical-input routing";
        return false;
    }
    return true;
}

bool Apex5Device::readRgbConfig(
    std::uint8_t slot,
    std::array<std::uint8_t, kRgbConfigSize>& outConfig,
    std::string& error) {
    if (!isOpen()) {
        error = "APEX device is not open";
        return false;
    }
    if (!mayControlProfiles(error)) {
        return false;
    }

    const auto lock = acquireWriteLock();
    const auto bufferSize = std::max<std::size_t>(
        kReportSize, transport_->info().inputReportLength);
    std::vector<std::uint8_t> input(bufferSize, 0);

    constexpr std::size_t kMaximumDrainReports = 64;
    for (std::size_t count = 0; count < kMaximumDrainReports; ++count) {
        std::size_t bytesRead = 0;
        std::string drainError;
        const auto status = transport_->readInputReport(
            input, std::chrono::milliseconds(0), bytesRead, drainError);
        if (status == platform::HidReadStatus::Timeout) break;
        if (status == platform::HidReadStatus::Error) {
            error = "Could not drain stale input before reading RGB: " + drainError;
            return false;
        }
    }

    if (!transport_->writeOutputReport(buildReadRgbConfig(slot, kRgbPacketSize), error)) {
        return false;
    }

    std::size_t packetsReceived = 0;
    std::vector<bool> received(kRgbPacketCount, false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);

    while (packetsReceived < kRgbPacketCount) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        if (remaining.count() <= 0) remaining = std::chrono::milliseconds(1);

        std::size_t bytesRead = 0;
        std::string readErr;
        const auto status = transport_->readInputReport(input, remaining, bytesRead, readErr);
        if (status == platform::HidReadStatus::Timeout) break;
        if (status == platform::HidReadStatus::Error) {
            error = "Error reading RGB config packet: " + readErr;
            return false;
        }

        const auto bytes = std::span<const std::uint8_t>(input.data(), bytesRead);
        std::size_t headerOffset = 0;
        bool foundHeader = false;
        for (std::size_t i = 0; i + 3 < bytes.size(); ++i) {
            if (bytes[i] == kMagic0 && bytes[i + 1] == kMagic1 && bytes[i + 2] == kCmdReadRgbConfig) {
                headerOffset = i;
                foundHeader = true;
                break;
            }
        }
        if (!foundHeader || headerOffset + 6 + kRgbPacketSize > bytes.size()) continue;

        const auto packIndex = bytes[headerOffset + 4];
        if (packIndex < kRgbPacketCount && !received[packIndex]) {
            received[packIndex] = true;
            ++packetsReceived;
            const auto destOffset = static_cast<std::size_t>(packIndex) * kRgbPacketSize;
            std::copy_n(bytes.begin() + headerOffset + 6, kRgbPacketSize, outConfig.begin() + destOffset);
        }
    }

    if (packetsReceived < kRgbPacketCount) {
        error = "Timed out waiting for RGB config packets (received " +
                std::to_string(packetsReceived) + "/" + std::to_string(kRgbPacketCount) + ")";
        return false;
    }
    return true;
}

bool Apex5Device::writeRgbConfig(
    std::uint8_t slot,
    std::span<const std::uint8_t> payload,
    std::string& error) {
    if (!isOpen()) {
        error = "APEX device is not open";
        return false;
    }
    if (!mayWriteEffects(error)) {
        return false;
    }
    if (payload.size() < kRgbConfigSize) {
        error = "RGB payload too small";
        return false;
    }

    const auto lock = acquireWriteLock();
    const auto startReport = buildWriteRgbStart(slot, 0, kRgbPacketCount, kRgbPacketSize);
    if (!transport_->writeOutputReport(startReport, error)) {
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    for (std::uint8_t packIndex = 0; packIndex < kRgbPacketCount; ++packIndex) {
        const auto offset = static_cast<std::size_t>(packIndex) * kRgbPacketSize;
        const auto chunk = std::span<const std::uint8_t>(payload.data() + offset, kRgbPacketSize);
        const auto packReport = buildWriteRgbPack(packIndex, chunk);
        if (!transport_->writeOutputReport(packReport, error)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

bool Apex5Device::setRgb(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                         std::string& error, std::uint8_t /*slot*/,
                         std::uint8_t /*brightness*/) {
    if (!isOpen()) {
        error = "APEX device is not open";
        return false;
    }
    if (!mayWriteEffects(error)) {
        return false;
    }

    // 0xF5 is the volatile, single-report LED command.  Do not use the
    // multi-packet profile writer here: lightbar feedback can arrive many
    // times per second, and repeatedly rewriting the profile interrupts the
    // controller's physical input stream.
    const auto lock = acquireWriteLock();
    return transport_->writeOutputReport(buildSetRgb(r, g, b), error);
}

bool Apex5Device::requestBatteryRefresh(std::string& error) {
    if (!isOpen()) {
        error = "APEX device is not open";
        return false;
    }
    if (!identity_ || !identity_->isApex5()) {
        error = "Battery refresh is supported on a verified Apex 5 only";
        return false;
    }
    const auto lock = acquireWriteLock();
    return transport_->writeOutputReport(Apex5Identity::buildRequest(), error);
}

bool Apex5Device::startAsyncWrites(std::string& error) {
    if (writer_.joinable() || (identity_ && !identity_->isApex4())) return true;
    if (!isOpen() || !mayWriteEffects(error)) return false;
    {
        std::lock_guard lock(queueMutex_);
        writerStopping_ = false;
        pendingLeftTrigger_.reset();
        pendingRightTrigger_.reset();
        pendingRumble_.reset();
        nextSlot_ = 0;
    }
    asyncWriteFailed_.store(false, std::memory_order_relaxed);
    asyncWriteRetries_.store(0, std::memory_order_relaxed);
    writer_ = std::thread([this] { writerLoop(); });
    return true;
}

void Apex5Device::stopAsyncWrites() noexcept {
    if (!writer_.joinable()) return;
    {
        std::lock_guard lock(queueMutex_);
        writerStopping_ = true;
    }
    queueSignal_.notify_all();
    writer_.join();
}

bool Apex5Device::queueTriggerRaw(const ForceTriggerCommand& command,
                                  std::string& error) {
    if (!writer_.joinable()) return setTriggerRaw(command, error);
    {
        std::lock_guard lock(queueMutex_);
        (command.side == TriggerSide::Left ? pendingLeftTrigger_
                                           : pendingRightTrigger_) = command;
    }
    queueSignal_.notify_one();
    return true;
}

bool Apex5Device::queueRumble(std::uint8_t lowFrequencyMotor,
                              std::uint8_t highFrequencyMotor,
                              std::string& error) {
    if (!writer_.joinable()) {
        return setRumble(lowFrequencyMotor, highFrequencyMotor, error);
    }
    {
        std::lock_guard lock(queueMutex_);
        pendingRumble_ = std::pair{lowFrequencyMotor, highFrequencyMotor};
    }
    queueSignal_.notify_one();
    return true;
}

std::uint64_t Apex5Device::asyncWriteRetries() const noexcept {
    return asyncWriteRetries_.load(std::memory_order_relaxed);
}

bool Apex5Device::takeAsyncWriteError(std::string& error) {
    if (!asyncWriteFailed_.exchange(false, std::memory_order_acq_rel)) return false;
    std::lock_guard lock(asyncErrorMutex_);
    error = asyncError_;
    return true;
}

void Apex5Device::writerLoop() {
    constexpr unsigned kMaximumConsecutiveFailures = 10;
    unsigned consecutiveFailures = 0;
    for (;;) {
        {
            std::unique_lock lock(queueMutex_);
            queueSignal_.wait(lock, [this] {
                return writerStopping_ || pendingLeftTrigger_ ||
                       pendingRightTrigger_ || pendingRumble_;
            });
            if (writerStopping_) return;
        }

        // Coalesce updates that arrive while the receiver's pacing window is
        // open, then choose the newest value rather than an obsolete one.
        constexpr auto kVendorWriteSpacing = std::chrono::milliseconds(25);
        if (lastVendorWriteAt_.time_since_epoch().count() != 0) {
            const auto since = std::chrono::steady_clock::now() - lastVendorWriteAt_;
            if (since < kVendorWriteSpacing) {
                std::this_thread::sleep_for(kVendorWriteSpacing - since);
            }
        }

        std::optional<ForceTriggerCommand> trigger;
        std::optional<std::pair<std::uint8_t, std::uint8_t>> rumble;
        {
            std::lock_guard lock(queueMutex_);
            if (writerStopping_) return;
            for (unsigned attempt = 0; attempt < 3; ++attempt) {
                const unsigned slot = (nextSlot_ + attempt) % 3;
                if (slot == 0 && pendingLeftTrigger_) {
                    trigger = std::exchange(pendingLeftTrigger_, std::nullopt);
                } else if (slot == 1 && pendingRightTrigger_) {
                    trigger = std::exchange(pendingRightTrigger_, std::nullopt);
                } else if (slot == 2 && pendingRumble_) {
                    rumble = std::exchange(pendingRumble_, std::nullopt);
                } else {
                    continue;
                }
                nextSlot_ = (slot + 1) % 3;
                break;
            }
        }
        if (!trigger && !rumble) continue;

        std::string error;
        const bool ok = trigger ? setTriggerRaw(*trigger, error)
                                : setRumble(rumble->first, rumble->second, error);
        if (ok) {
            consecutiveFailures = 0;
            continue;
        }

        {
            std::lock_guard lock(queueMutex_);
            if (trigger) {
                auto& pending = trigger->side == TriggerSide::Left
                    ? pendingLeftTrigger_ : pendingRightTrigger_;
                if (!pending) pending = trigger;
            } else if (!pendingRumble_) {
                pendingRumble_ = rumble;
            }
        }
        asyncWriteRetries_.fetch_add(1, std::memory_order_relaxed);
        if (++consecutiveFailures < kMaximumConsecutiveFailures) continue;
        {
            std::lock_guard lock(asyncErrorMutex_);
            asyncError_ = std::move(error);
        }
        asyncWriteFailed_.store(true, std::memory_order_release);
        consecutiveFailures = 0;
    }
}

} // namespace asb::flydigi
