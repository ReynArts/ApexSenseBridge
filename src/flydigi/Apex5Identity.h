#pragma once

#include "flydigi/Apex5Protocol.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace asb::flydigi {

class Apex5Identity {
public:
    [[nodiscard]] static Report buildRequest();
    [[nodiscard]] static std::optional<Apex5Identity> parseReply(
        std::span<const std::uint8_t> report);
    [[nodiscard]] static bool isApex5DeviceType(std::uint8_t deviceType) noexcept;

    [[nodiscard]] std::uint8_t deviceType() const noexcept { return deviceType_; }
    [[nodiscard]] std::uint8_t connectionTypeRaw() const noexcept { return connectionType_; }
    [[nodiscard]] bool isWired() const noexcept { return connectionType_ == 1; }
    [[nodiscard]] bool isApex5() const noexcept { return isApex5DeviceType(deviceType_); }
    [[nodiscard]] bool supportsAdaptiveTriggers() const noexcept { return isApex5(); }
    [[nodiscard]] std::uint8_t batteryLevel() const noexcept { return batteryLevel_; }
    [[nodiscard]] bool isCharging() const noexcept { return charging_; }
    [[nodiscard]] std::string describe() const;

private:
    Apex5Identity(std::uint8_t deviceType,
                  std::uint8_t connectionType,
                  std::uint8_t batteryLevel,
                  bool charging) noexcept;

    std::uint8_t deviceType_ = 0;
    std::uint8_t connectionType_ = 0;
    std::uint8_t batteryLevel_ = 0;
    bool charging_ = false;
};

} // namespace asb::flydigi
