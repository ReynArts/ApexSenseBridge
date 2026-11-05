#pragma once

#include "dualsense/DualSenseFeedback.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace asb::cli {

struct UdpTriggerServerStats {
    std::uint64_t packets = 0;
    std::uint64_t accepted = 0;
    std::uint64_t rejected = 0;
};

class UdpTriggerServer {
public:
    using Handler = std::function<void(const asb::dualsense::DualSenseFeedback&)>;

    UdpTriggerServer(std::string bindAddress, std::uint16_t port, Handler handler);
    ~UdpTriggerServer();

    UdpTriggerServer(const UdpTriggerServer&) = delete;
    UdpTriggerServer& operator=(const UdpTriggerServer&) = delete;

    [[nodiscard]] bool start(std::string& error);
    void stop() noexcept;
    [[nodiscard]] UdpTriggerServerStats stats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace asb::cli
