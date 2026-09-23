#pragma once

#include "dualsense/DualSenseFeedback.h"
#include "flydigi/Apex5Device.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace asb::dualsense {

struct LightbarBridgeStats {
    std::uint64_t updates = 0;
    std::uint64_t writes = 0;
    std::uint64_t deduplicated = 0;
    std::uint64_t writeFailures = 0;
};

class LightbarBridge {
public:
    explicit LightbarBridge(flydigi::Apex5Device& device, std::uint8_t slot = 0);
    ~LightbarBridge();

    LightbarBridge(const LightbarBridge&) = delete;
    LightbarBridge& operator=(const LightbarBridge&) = delete;

    void handle(const DualSenseFeedback& feedback);
    void restore();

    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] LightbarBridgeStats stats() const noexcept;

private:
    void workerLoop() noexcept;
    bool writeWorkingColor(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                           std::string& error);
    void recordFailure(std::string error) noexcept;

    flydigi::Apex5Device& device_;
    std::uint8_t slot_ = 0;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic_bool running_{false};

    std::array<std::uint8_t, flydigi::kRgbConfigSize> backupConfig_{};
    std::array<std::uint8_t, flydigi::kRgbConfigSize> workingConfig_{};
    bool hasBackup_ = false;
    bool restored_ = false;

    bool hasTarget_ = false;
    std::uint8_t targetRed_ = 0;
    std::uint8_t targetGreen_ = 0;
    std::uint8_t targetBlue_ = 0;

    bool hasWritten_ = false;
    std::uint8_t writtenRed_ = 0;
    std::uint8_t writtenGreen_ = 0;
    std::uint8_t writtenBlue_ = 0;

    std::atomic_bool failed_{false};
    mutable std::mutex errorMutex_;
    std::string error_;

    std::atomic_uint64_t updates_{0};
    std::atomic_uint64_t writes_{0};
    std::atomic_uint64_t deduplicated_{0};
    std::atomic_uint64_t writeFailures_{0};
};

} // namespace asb::dualsense
