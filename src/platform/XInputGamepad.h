#pragma once

#include "dualsense/DualSenseInput.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace asb::platform {

class XInputGamepad {
public:
    virtual ~XInputGamepad() = default;
    virtual bool poll(dualsense::DualSenseInputState& state, std::string& error) = 0;
    [[nodiscard]] virtual unsigned int index() const noexcept = 0;
};

std::vector<unsigned int> connectedXInputGamepads();
std::unique_ptr<XInputGamepad> openXInputGamepad(
    std::optional<unsigned int> requestedIndex, std::string& error);
std::unique_ptr<XInputGamepad> openXInputGamepadForDevice(
    std::uint16_t vendorId,
    std::uint16_t productId,
    std::optional<unsigned int> requestedIndex,
    std::string& error);

} // namespace asb::platform
