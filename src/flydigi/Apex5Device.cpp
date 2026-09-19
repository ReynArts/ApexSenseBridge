#include "flydigi/Apex5Device.h"

#include "flydigi/Apex4Protocol.h"
#include "flydigi/Apex5Protocol.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

    // V1 controllers can occasionally miss a command write while streaming
    // input, especially over the 2.4 GHz receiver, so a dropped request has to
    // be retried. Retrying eagerly, however, is what actually breaks the
    // exchange: measured against an Apex 4 on the dongle, the reply either
    // arrives within about 8 ms or never arrives at all, and a burst of
    // back-to-back requests leaves the controller silent for roughly thirty
    // seconds afterwards. A rested controller answers the first request; a
    // hammered one answers nothing, which is why a few widely spaced attempts
    // recover far more sessions than many tightly packed ones.
    constexpr auto kApex4AttemptTimeout = std::chrono::milliseconds(250);
    constexpr auto kApex4RetryDelay = std::chrono::milliseconds(900);
    constexpr auto kApex5AttemptTimeout = std::chrono::milliseconds(600);
    constexpr std::size_t kMaximumReplies = 4096;
    const std::size_t maximumAttempts = apex4 ? kApex4IdentityAttempts : 1;
    Apex4IdentityObservation apex4Observation;
    for (std::size_t attempt = 0; attempt < maximumAttempts; ++attempt) {
        if (apex4 && attempt != 0) {
            // Give the controller quiet air before asking again. This costs
            // nothing in the common case, where the first request is answered.
            std::this_thread::sleep_for(kApex4RetryDelay);
        }
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
        ? "No valid command 0xEC Apex 4 identity reply arrived after " +
          std::to_string(maximumAttempts) + " spaced attempts;" +
          apex4Observation.describe() +
          "; the controller stops answering vendor queries for about 30 seconds "
          "after a burst of them, so leave it idle briefly before retrying, and "
          "use USB/dongle DInput mode with Flydigi Space Station closed"
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

namespace {

// Diagnostics only: ASB_DUMP_PAD_WRITES=<path> records every vendor command
// this process sends to the pad, with a monotonic timestamp. Nothing about the
// traffic changes; the writes are observable nowhere else because hidraw
// readers only ever see the input direction.
void logPadWrite(std::span<const std::uint8_t> report, const char* origin) {
    static std::FILE* sink = [] () -> std::FILE* {
        const char* path = std::getenv("ASB_DUMP_PAD_WRITES");
        return path ? std::fopen(path, "w") : nullptr;
    }();
    if (!sink) {
        return;
    }
    static const auto start = std::chrono::steady_clock::now();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start).count();
    std::fprintf(sink, "%lld %s", static_cast<long long>(us), origin);
    for (const auto byte : report) {
        std::fprintf(sink, " %02x", byte);
    }
    std::fprintf(sink, "\n");
    std::fflush(sink);
}

} // namespace

namespace {

// 10 ms was the shortest gap that worked in every round of a blind hardware
// check; 25 ms keeps a margin. The cost is bounded: a full update is a handful
// of commands, and it lands well inside the time it takes to raise a weapon.
constexpr auto kVendorWriteSpacing = std::chrono::milliseconds(25);

} // namespace

bool Apex5Device::writeSpacedOutputReport(std::span<const std::uint8_t> report,
                                          std::string& error) {
    const auto now = std::chrono::steady_clock::now();
    if (lastVendorWriteAt_.time_since_epoch().count() != 0) {
        const auto since = now - lastVendorWriteAt_;
        if (since < kVendorWriteSpacing) {
            // Waiting rather than dropping: the command that would be dropped is
            // the newest one, which is the one that matters.
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
    if (identity_->isApex4()) {
        const auto report4 = buildApex4ForceTrigger(effect, kApex4ApplyFlag);
        logPadWrite(report4, "setTrigger");
        return writeSpacedOutputReport(report4, error);
    }
    const auto report = buildForceTrigger(effect, true);
    logPadWrite(report, "setTrigger");
    return writeSpacedOutputReport(report, error);
}

bool Apex5Device::setTriggerRaw(const ForceTriggerCommand& command, std::string& error) {
    if (!isOpen()) {
        error = "APEX device is not open";
        return false;
    }
    if (!mayWriteEffects(error)) {
        return false;
    }
    if (identity_->isApex4()) {
        const auto raw4 = buildApex4ForceTriggerRaw(command, kApex4ApplyFlag);
        logPadWrite(raw4, "setTriggerRaw");
        return writeSpacedOutputReport(raw4, error);
    }
    const auto raw5 = buildForceTriggerRaw(command, true);
    logPadWrite(raw5, "setTriggerRaw");
    return writeSpacedOutputReport(raw5, error);
}

bool Apex5Device::clearTrigger(TriggerSide side, std::string& error) {
    if (!isOpen()) {
        error = "APEX device is not open";
        return false;
    }
    if (!mayWriteEffects(error)) {
        return false;
    }
    if (identity_->isApex4()) {
        const auto clr4 = buildApex4Normal(side);
        logPadWrite(clr4, "clearTrigger");
        return writeSpacedOutputReport(clr4, error);
    }
    const auto clr5 = buildNormal(side);
    logPadWrite(clr5, "clearTrigger");
    return writeSpacedOutputReport(clr5, error);
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
    if (identity_->isApex4()) {
        const auto rmb4 = buildApex4Rumble(lowFrequencyMotor, highFrequencyMotor);
        logPadWrite(rmb4, "setRumble");
        return writeSpacedOutputReport(rmb4, error);
    }
    const auto rmb5 = buildRumble(lowFrequencyMotor, highFrequencyMotor);
    logPadWrite(rmb5, "setRumble");
    return writeSpacedOutputReport(rmb5, error);
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

Apex5Device::~Apex5Device() {
    stopAsyncWrites();
}

Apex5Device::Apex5Device(Apex5Device&& other) noexcept {
    other.stopAsyncWrites();
    transport_ = std::move(other.transport_);
    identity_ = std::move(other.identity_);
    lastVendorWriteAt_ = other.lastVendorWriteAt_;
}

Apex5Device& Apex5Device::operator=(Apex5Device&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    stopAsyncWrites();
    other.stopAsyncWrites();
    transport_ = std::move(other.transport_);
    identity_ = std::move(other.identity_);
    lastVendorWriteAt_ = other.lastVendorWriteAt_;
    return *this;
}

bool Apex5Device::startAsyncWrites(std::string& error) {
    if (writer_.joinable()) {
        return true;
    }
    if (!transport_) {
        error = "APEX device is not open";
        return false;
    }
    {
        std::lock_guard lock(queueMutex_);
        writerStopping_ = false;
        pendingLeftTrigger_.reset();
        pendingRightTrigger_.reset();
        pendingRumble_.reset();
        nextSlot_ = 0;
    }
    asyncWriteFailed_.store(false, std::memory_order_relaxed);
    writer_ = std::thread([this] { writerLoop(); });
    return true;
}

void Apex5Device::stopAsyncWrites() noexcept {
    if (!writer_.joinable()) {
        return;
    }
    {
        std::lock_guard lock(queueMutex_);
        writerStopping_ = true;
    }
    queueSignal_.notify_all();
    writer_.join();
}

bool Apex5Device::queueTriggerRaw(const ForceTriggerCommand& command,
                                  std::string& error) {
    if (!writer_.joinable()) {
        return setTriggerRaw(command, error);
    }
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

bool Apex5Device::takeAsyncWriteError(std::string& error) {
    if (!asyncWriteFailed_.exchange(false, std::memory_order_acq_rel)) {
        return false;
    }
    std::lock_guard lock(asyncErrorMutex_);
    error = asyncError_;
    return true;
}

std::uint64_t Apex5Device::asyncWriteRetries() const noexcept {
    return asyncWriteRetries_.load(std::memory_order_relaxed);
}

void Apex5Device::writerLoop() {
    // Consecutive, not total: an occasional dropped command is normal, a run of
    // them is not.
    constexpr unsigned kMaxConsecutiveWriteFailures = 10;
    unsigned consecutiveFailures = 0;
    for (;;) {
        // Wait for something to send, then spend the pad's spacing *before*
        // choosing what to send. Choosing first and sleeping afterwards meant a
        // command could be superseded while it waited and still go out: the
        // slot held one value at most, but that one value was already committed
        // and burnt the next transmission window on a state the game had
        // abandoned. Sleeping first lets every update during the wait land in
        // the slot, and the newest one is what leaves.
        {
            std::unique_lock lock(queueMutex_);
            queueSignal_.wait(lock, [this] {
                return writerStopping_ || pendingLeftTrigger_ || pendingRightTrigger_ ||
                       pendingRumble_;
            });
            if (writerStopping_) {
                return;
            }
        }

        if (lastVendorWriteAt_.time_since_epoch().count() != 0) {
            const auto since = std::chrono::steady_clock::now() - lastVendorWriteAt_;
            if (since < kVendorWriteSpacing) {
                std::this_thread::sleep_for(kVendorWriteSpacing - since);
            }
        }

        std::optional<ForceTriggerCommand> trigger;
        std::optional<std::pair<std::uint8_t, std::uint8_t>> rumble;
        {
            std::unique_lock lock(queueMutex_);
            if (writerStopping_) {
                return;
            }
            // Round-robin rather than a fixed order. Writing one slot first
            // every time is what made the left trigger the command that always
            // went missing, and starving a slot under a fast game would be the
            // same bug wearing the spacing as a disguise.
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
        if (!trigger && !rumble) {
            continue;   // woken with nothing left to send
        }

        // The spacing is already satisfied, so these do not wait again.
        std::string error;
        const bool ok = trigger ? setTriggerRaw(*trigger, error)
                                : setRumble(rumble->first, rumble->second, error);
        if (ok) {
            consecutiveFailures = 0;
            continue;
        }

        // Put it back and let the next cycle send it again. This pad drops a
        // command now and then - that is the whole reason the spacing exists -
        // and ending a session mid-game over one of them is a worse failure
        // than the failure. A value the game has already superseded is not
        // restored: the newer one is what should go.
        {
            std::lock_guard lock(queueMutex_);
            if (trigger) {
                auto& slot = trigger->side == TriggerSide::Left ? pendingLeftTrigger_
                                                                : pendingRightTrigger_;
                if (!slot) {
                    slot = trigger;
                }
            } else if (!pendingRumble_) {
                pendingRumble_ = rumble;
            }
        }
        asyncWriteRetries_.fetch_add(1, std::memory_order_relaxed);

        // Retries are spaced like any other write, so this is about a quarter
        // of a second of an output path that will not take anything at all.
        if (++consecutiveFailures < kMaxConsecutiveWriteFailures) {
            continue;
        }
        {
            std::lock_guard lock(asyncErrorMutex_);
            asyncError_ = std::move(error);
        }
        asyncWriteFailed_.store(true, std::memory_order_release);
        consecutiveFailures = 0;
    }
}

} // namespace asb::flydigi
