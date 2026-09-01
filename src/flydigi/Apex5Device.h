#pragma once

#include "core/DeviceInfo.h"
#include "core/TriggerEffect.h"
#include "flydigi/Apex5Identity.h"
#include "platform/HidTransport.h"

#include <memory>
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

private:
    [[nodiscard]] bool mayWriteEffects(std::string& error) const;

    TransportPtr transport_{};
    std::optional<Apex5Identity> identity_{};
};

} // namespace asb::flydigi
