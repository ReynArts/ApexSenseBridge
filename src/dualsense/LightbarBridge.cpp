#include "dualsense/LightbarBridge.h"

#include <cmath>

namespace asb::dualsense {

LightbarBridge::LightbarBridge(flydigi::Apex5Device& device, std::uint8_t slot)
    : device_(device), slot_(slot) {
    worker_ = std::thread(&LightbarBridge::workerLoop, this);
}

LightbarBridge::~LightbarBridge() {
    restore();
}

void LightbarBridge::restore() {
    if (running_.exchange(false)) {
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        std::string err;
        device_.applyProfile(slot_, err);
    }
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
            const auto nextSend = lastSend + kMinInterval;
            cv_.wait_until(lock, nextSend, [this] {
                return !running_.load(std::memory_order_relaxed) ||
                       (hasTarget_ && (!hasWritten_ ||
                                       targetRed_ != writtenRed_ ||
                                       targetGreen_ != writtenGreen_ ||
                                       targetBlue_ != writtenBlue_));
            });

            if (!running_.load(std::memory_order_relaxed)) {
                break;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= nextSend && hasTarget_ &&
                (!hasWritten_ || targetRed_ != writtenRed_ ||
                 targetGreen_ != writtenGreen_ || targetBlue_ != writtenBlue_)) {
                r = targetRed_;
                g = targetGreen_;
                b = targetBlue_;
                shouldWrite = true;
            }
        }

        if (shouldWrite) {
            std::string err;
            if (device_.setRgb(r, g, b, err, slot_)) {
                std::lock_guard lock(mutex_);
                writtenRed_ = r;
                writtenGreen_ = g;
                writtenBlue_ = b;
                hasWritten_ = true;
                writes_.fetch_add(1, std::memory_order_relaxed);
                lastSend = std::chrono::steady_clock::now();
            } else {
                writeFailures_.fetch_add(1, std::memory_order_relaxed);
                failed_.store(true, std::memory_order_relaxed);
                std::lock_guard errLock(errorMutex_);
                error_ = std::move(err);
            }
        }
    }
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
    result.lastRed = writtenRed_;
    result.lastGreen = writtenGreen_;
    result.lastBlue = writtenBlue_;
    return result;
}

} // namespace asb::dualsense
