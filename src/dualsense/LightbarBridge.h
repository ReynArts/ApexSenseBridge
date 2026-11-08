#pragma once

#include "dualsense/DualSenseFeedback.h"
#include "flydigi/Apex5Device.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace asb::dualsense {

struct LightbarBridgeStats {
    std::uint64_t updates = 0;
    std::uint64_t writes = 0;
    std::uint64_t deduplicated = 0;
    std::uint64_t writeFailures = 0;
};

class LightbarBridge {
public:
    // maxBrightnessPercent caps the brightness written with every color. The
    // brightness saved in the controller profile (Flydigi Space Station) is
    // kept when it is lower.
    explicit LightbarBridge(flydigi::Apex5Device& device, std::uint8_t slot = 0,
                            std::uint8_t maxBrightnessPercent = 100);
    ~LightbarBridge();

    LightbarBridge(const LightbarBridge&) = delete;
    LightbarBridge& operator=(const LightbarBridge&) = delete;

    void handle(const DualSenseFeedback& feedback);
    void restore();
    void suspend();
    bool resume();

    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] LightbarBridgeStats stats() const noexcept;
    [[nodiscard]] std::uint8_t brightness() const noexcept;
    [[nodiscard]] std::uint8_t storedBrightness() const noexcept;
    [[nodiscard]] std::uint8_t maxBrightness() const noexcept;

private:
    void workerLoop() noexcept;
    bool initializeApex6(std::string& error);
    bool writeApex6Color(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                         std::string& error);
    void restoreApex6();
    [[nodiscard]] static std::filesystem::path apex6BackupPath();
    bool writeWorkingColor(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                           std::string& error);
    void recordFailure(std::string error) noexcept;

    flydigi::Apex5Device& device_;
    std::uint8_t slot_ = 0;
    std::uint8_t maxBrightness_ = 100;
    std::uint8_t storedBrightness_ = 100;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic_bool running_{false};

    std::array<std::uint8_t, flydigi::kRgbConfigSize> backupConfig_{};
    std::array<std::uint8_t, flydigi::kRgbConfigSize> workingConfig_{};
    bool hasBackup_ = false;
    bool restored_ = false;

    bool apex6_ = false;
    bool apex6Dirty_ = false;
    bool apex6Vibration_ = false;
    std::vector<std::uint8_t> apex6Backup_;
    std::filesystem::path apex6BackupFile_;

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
