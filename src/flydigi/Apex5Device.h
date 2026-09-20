#pragma once

#include "core/DeviceInfo.h"
#include "core/TriggerEffect.h"
#include "flydigi/Apex5Identity.h"
#include "platform/HidTransport.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
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
    Apex5Device(Apex5Device&&) noexcept = default;
    Apex5Device& operator=(Apex5Device&&) noexcept = default;

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
    bool setRgb(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                std::string& error, std::uint8_t slot = 0,
                std::uint8_t brightness = 100);
    bool readRgbConfig(std::uint8_t slot,
                       std::array<std::uint8_t, kRgbConfigSize>& outConfig,
                       std::string& error);
    bool writeRgbConfig(std::uint8_t slot,
                        std::span<const std::uint8_t> payload,
                        std::string& error);
    bool readProfileStatus(ProfileStatus& status, std::string& error);
    bool applyProfile(std::uint8_t slot, std::string& error);
    bool readInputTransportStatus(InputTransportStatus& status,
                                  std::string& error);
    bool setInputTransport(bool controllerData, bool rawData,
                           std::string& error);
    bool requestBatteryRefresh(std::string& error);

private:
    [[nodiscard]] bool mayWriteEffects(std::string& error) const;
    [[nodiscard]] bool mayControlProfiles(std::string& error) const;
    [[nodiscard]] bool usesApex4Protocol() const noexcept;
    [[nodiscard]] std::unique_lock<std::recursive_mutex> acquireWriteLock() const noexcept;

    TransportPtr transport_{};
    std::optional<Apex5Identity> identity_{};
    mutable std::unique_ptr<std::recursive_mutex> writeMutex_{std::make_unique<std::recursive_mutex>()};
};

} // namespace asb::flydigi
