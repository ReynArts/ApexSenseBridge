#pragma once

#include "core/DeviceInfo.h"
#include "core/TriggerEffect.h"
#include "flydigi/Apex5Identity.h"
#include "platform/HidTransport.h"

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>
#include <utility>
#include <vector>

namespace asb::flydigi {

struct TransportDeleter {
    void operator()(platform::HidTransport* transport) const noexcept;
};
using TransportPtr = std::unique_ptr<platform::HidTransport, TransportDeleter>;

class Apex5Device {
public:
    // Historical API name retained for source compatibility. This transport
    // now supports both Apex 5 (Flydigi V2) and Apex 4 (Flydigi V1/DInput).
    Apex5Device() = default;
    explicit Apex5Device(TransportPtr transport);

    Apex5Device(const Apex5Device&) = delete;
    Apex5Device& operator=(const Apex5Device&) = delete;
    ~Apex5Device();
    Apex5Device(Apex5Device&& other) noexcept;
    Apex5Device& operator=(Apex5Device&& other) noexcept;

    [[nodiscard]] static std::vector<HidDeviceInfo> findCandidates(std::string& error);
    [[nodiscard]] static std::optional<Apex5Device> open(const HidDeviceInfo& info, std::string& error);

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] const HidDeviceInfo& info() const;
    [[nodiscard]] const std::optional<Apex5Identity>& identity() const noexcept;

    bool verifyIdentity(std::string& error);

    bool setTrigger(const TriggerEffect& effect, std::string& error);
    bool setTriggerRaw(const ForceTriggerCommand& command, std::string& error);
    bool clearTrigger(TriggerSide side, std::string& error);
    bool clearAll(std::string& error);
    bool setRumble(std::uint8_t lowFrequencyMotor,
                   std::uint8_t highFrequencyMotor,
                   std::string& error);
    bool stopRumble(std::string& error);
    bool readRgbConfig(std::uint8_t slot,
                       std::array<std::uint8_t, kRgbConfigSize>& outConfig,
                       std::string& error);
    bool writeRgbConfig(std::uint8_t slot,
                        std::span<const std::uint8_t> payload,
                        std::string& error);
    bool writeRgbConfigRange(std::uint8_t slot,
                             std::uint8_t firstPacket,
                             std::uint8_t packetCount,
                             std::span<const std::uint8_t> payload,
                             std::string& error);
    bool readProfileStatus(ProfileStatus& status, std::string& error);
    bool applyProfile(std::uint8_t slot, std::string& error);
    bool readInputTransportStatus(InputTransportStatus& status,
                                  std::string& error);
    bool setInputTransport(bool controllerData, bool rawData,
                           std::string& error);
    bool requestBatteryRefresh(std::string& error);

    // The Apex 4 receiver needs paced vendor writes. The bridge queues the
    // newest value per output so its feedback callback never blocks and stale
    // effects are coalesced instead of replayed later.
    bool startAsyncWrites(std::string& error);
    void stopAsyncWrites() noexcept;
    bool queueTriggerRaw(const ForceTriggerCommand& command, std::string& error);
    bool queueRumble(std::uint8_t lowFrequencyMotor,
                     std::uint8_t highFrequencyMotor,
                     std::string& error);
    [[nodiscard]] std::uint64_t asyncWriteRetries() const noexcept;
    bool takeAsyncWriteError(std::string& error);

private:
    [[nodiscard]] bool mayWriteEffects(std::string& error) const;
    [[nodiscard]] bool mayControlProfiles(std::string& error) const;
    [[nodiscard]] bool usesApex4Protocol() const noexcept;
    [[nodiscard]] std::unique_lock<std::recursive_mutex> acquireWriteLock() const noexcept;
    [[nodiscard]] bool writeSpacedOutputReport(std::span<const std::uint8_t> report,
                                               std::string& error);
    void writerLoop();

    TransportPtr transport_{};
    std::optional<Apex5Identity> identity_{};
    mutable std::unique_ptr<std::recursive_mutex> writeMutex_{std::make_unique<std::recursive_mutex>()};
    std::chrono::steady_clock::time_point lastVendorWriteAt_{};
    std::thread writer_{};
    std::mutex queueMutex_{};
    std::condition_variable queueSignal_{};
    std::optional<ForceTriggerCommand> pendingLeftTrigger_{};
    std::optional<ForceTriggerCommand> pendingRightTrigger_{};
    std::optional<std::pair<std::uint8_t, std::uint8_t>> pendingRumble_{};
    unsigned nextSlot_ = 0;
    bool writerStopping_ = false;
    std::atomic<bool> asyncWriteFailed_{false};
    std::atomic<std::uint64_t> asyncWriteRetries_{0};
    std::mutex asyncErrorMutex_{};
    std::string asyncError_{};
};

} // namespace asb::flydigi
