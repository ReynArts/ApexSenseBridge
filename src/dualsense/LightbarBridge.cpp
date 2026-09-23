#include "dualsense/LightbarBridge.h"

#include <algorithm>
#include <cmath>

namespace asb::dualsense {

LightbarBridge::LightbarBridge(flydigi::Apex5Device& device, std::uint8_t slot)
    : device_(device), slot_(slot) {
    std::string error;
    if (!device_.readRgbConfig(slot_, backupConfig_, error)) {
        recordFailure(std::move(error));
        return;
    }
    workingConfig_ = backupConfig_;
    hasBackup_ = true;
    running_.store(true, std::memory_order_relaxed);
    worker_ = std::thread(&LightbarBridge::workerLoop, this);
}

LightbarBridge::~LightbarBridge() {
    restore();
}

void LightbarBridge::restore() {
    if (running_.exchange(false, std::memory_order_relaxed)) {
        cv_.notify_all();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    if (!hasBackup_ || restored_) return;

    restored_ = true;
    std::string error;
    auto latchedBackup = backupConfig_;
    latchedBackup[2] = 1;
    if (!device_.writeRgbConfig(slot_, latchedBackup, error) ||
        !device_.writeRgbConfigRange(slot_, 0, 1, backupConfig_, error) ||
        !device_.applyProfile(slot_, error)) {
        recordFailure(std::move(error));
        return;
    }
    workingConfig_ = backupConfig_;
}

void LightbarBridge::handle(const DualSenseFeedback& feedback) {
    if (!feedback.hasLightbarColor() || !running_.load(std::memory_order_relaxed)) {
        return;
    }

    updates_.fetch_add(1, std::memory_order_relaxed);

    const std::uint8_t r = feedback.lightbarRed;
    const std::uint8_t g = feedback.lightbarGreen;
    const std::uint8_t b = feedback.lightbarBlue;

    {
        std::lock_guard lock(mutex_);
        if (hasTarget_) {
            const int dr = std::abs(static_cast<int>(r) - static_cast<int>(targetRed_));
            const int dg = std::abs(static_cast<int>(g) - static_cast<int>(targetGreen_));
            const int db = std::abs(static_cast<int>(b) - static_cast<int>(targetBlue_));
            if (dr < 3 && dg < 3 && db < 3) {
                deduplicated_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        targetRed_ = r;
        targetGreen_ = g;
        targetBlue_ = b;
        hasTarget_ = true;
    }
    cv_.notify_one();
}

void LightbarBridge::workerLoop() noexcept {
    using namespace std::chrono_literals;
    constexpr auto kMinInterval = 80ms;
    auto lastSend = std::chrono::steady_clock::now() - kMinInterval;

    while (running_.load(std::memory_order_relaxed)) {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        bool shouldWrite = false;

        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] {
                return !running_.load(std::memory_order_relaxed) ||
                       (hasTarget_ && (!hasWritten_ ||
                                       targetRed_ != writtenRed_ ||
                                       targetGreen_ != writtenGreen_ ||
                                       targetBlue_ != writtenBlue_));
            });

            if (!running_.load(std::memory_order_relaxed)) {
                break;
            }

            const auto nextSend = lastSend + kMinInterval;
            if (std::chrono::steady_clock::now() < nextSend) {
                cv_.wait_until(lock, nextSend, [this] {
                    return !running_.load(std::memory_order_relaxed);
                });
                if (!running_.load(std::memory_order_relaxed)) {
                    break;
                }
            }

            r = targetRed_;
            g = targetGreen_;
            b = targetBlue_;
            shouldWrite = !hasWritten_ || r != writtenRed_ ||
                          g != writtenGreen_ || b != writtenBlue_;
        }

        if (shouldWrite) {
            std::string err;
            if (writeWorkingColor(r, g, b, err)) {
                std::lock_guard lock(mutex_);
                writtenRed_ = r;
                writtenGreen_ = g;
                writtenBlue_ = b;
                hasWritten_ = true;
                writes_.fetch_add(1, std::memory_order_relaxed);
                lastSend = std::chrono::steady_clock::now();
            } else {
                recordFailure(std::move(err));
                running_.store(false, std::memory_order_relaxed);
                break;
            }
        }
    }
}

bool LightbarBridge::writeWorkingColor(
    std::uint8_t r, std::uint8_t g, std::uint8_t b, std::string& error) {
    constexpr std::size_t kHeaderSize = 20;
    auto loaded = workingConfig_;
    loaded[2] = 1;
    loaded[3] = 0;
    loaded[4] = 9;
    loaded[5] = 1;
    loaded[6] = 100;
    loaded[7] = static_cast<std::uint8_t>(flydigi::kApex5LedCount);
    loaded[8] = 4;
    loaded[9] = 0;
    std::fill(loaded.begin() + 10, loaded.begin() + kHeaderSize,
              std::uint8_t{0xFF});
    for (std::size_t offset = kHeaderSize;
         offset + 2 < loaded.size(); offset += 3) {
        loaded[offset] = r;
        loaded[offset + 1] = g;
        loaded[offset + 2] = b;
    }
    if (!device_.writeRgbConfig(slot_, loaded, error)) {
        return false;
    }
    workingConfig_ = loaded;

    auto visible = workingConfig_;
    visible[2] = 0;
    if (!device_.writeRgbConfigRange(slot_, 0, 1, visible, error)) {
        return false;
    }
    workingConfig_ = visible;
    return true;
}

void LightbarBridge::recordFailure(std::string error) noexcept {
    writeFailures_.fetch_add(1, std::memory_order_relaxed);
    failed_.store(true, std::memory_order_relaxed);
    std::lock_guard lock(errorMutex_);
    error_ = std::move(error);
}

bool LightbarBridge::failed() const noexcept {
    return failed_.load(std::memory_order_relaxed);
}

std::string LightbarBridge::error() const {
    std::lock_guard lock(errorMutex_);
    return error_;
}

LightbarBridgeStats LightbarBridge::stats() const noexcept {
    std::lock_guard lock(mutex_);
    LightbarBridgeStats result{};
    result.updates = updates_.load(std::memory_order_relaxed);
    result.writes = writes_.load(std::memory_order_relaxed);
    result.deduplicated = deduplicated_.load(std::memory_order_relaxed);
    result.writeFailures = writeFailures_.load(std::memory_order_relaxed);
    return result;
}

} // namespace asb::dualsense
