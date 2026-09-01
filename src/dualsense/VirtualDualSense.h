#pragma once

#include "dualsense/DualSenseFeedback.h"
#include "dualsense/DualSenseInput.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace asb::dualsense {

struct VirtualDualSenseOptions {
    std::filesystem::path viiperExecutable;
    std::uint16_t apiPort = 3242;
};

struct VirtualDualSenseStats {
    bool connected = false;
    std::uint64_t inputUpdates = 0;
    std::uint64_t outputReports = 0;
    std::uint64_t triggerReports = 0;
    std::uint64_t rumbleReports = 0;
    std::uint64_t audioHapticsFrames = 0;
    std::uint64_t audioHapticsDelivered = 0;
    std::uint64_t audioHapticsCoalesced = 0;
    std::uint64_t malformedFrames = 0;
    std::uint64_t unknownFrames = 0;
    std::string backendVersion;
};

class VirtualDualSense {
public:
    using FeedbackHandler = std::function<void(const DualSenseFeedback&)>;

    virtual ~VirtualDualSense() = default;
    virtual bool open(std::string& error, FeedbackHandler handler = {}) = 0;
    virtual void close() noexcept = 0;
    virtual bool updateInput(const DualSenseInputState& state, std::string& error) = 0;
    [[nodiscard]] virtual bool connected() const noexcept = 0;
    virtual VirtualDualSenseStats stats() const = 0;
};

std::unique_ptr<VirtualDualSense> createVirtualDualSense(VirtualDualSenseOptions options = {});

} // namespace asb::dualsense
