#include "dualsense/LightbarBridge.h"

#include "flydigi/Apex6Lighting.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>
#include <thread>

namespace asb::dualsense {

namespace {

constexpr auto kApex6MinInterval = std::chrono::milliseconds(1500);
constexpr std::uint64_t kApex6MaximumWrites = 300;
constexpr int kApex6ColorTolerance = 12;

} // namespace

LightbarBridge::LightbarBridge(flydigi::Apex5Device& device, std::uint8_t slot,
                               std::uint8_t maxBrightnessPercent)
    : device_(device),
      slot_(slot),
      maxBrightness_((std::min)(maxBrightnessPercent, std::uint8_t{100})) {
    apex6_ = device_.identity() && device_.identity()->isApex6();
    if (apex6_) {
        std::string apex6Error;
        if (!initializeApex6(apex6Error)) {
            recordFailure(std::move(apex6Error));
            return;
        }
        hasBackup_ = true;
        running_.store(true, std::memory_order_relaxed);
        worker_ = std::thread(&LightbarBridge::workerLoop, this);
        return;
    }
    std::string error;
    if (!device_.readRgbConfig(slot_, backupConfig_, error)) {
        recordFailure(std::move(error));
        return;
    }
    storedBrightness_ = (std::min)(backupConfig_[flydigi::kRgbBrightnessOffset],
                                   std::uint8_t{100});
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
    if (apex6_) {
        restoreApex6();
        return;
    }
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

void LightbarBridge::suspend() {
    if (running_.exchange(false, std::memory_order_relaxed)) {
        cv_.notify_all();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool LightbarBridge::resume() {
    if (!hasBackup_ || restored_) return false;
    suspend();
    {
        std::lock_guard lock(mutex_);
        hasWritten_ = false;
        workingConfig_ = backupConfig_;
    }
    failed_.store(false, std::memory_order_relaxed);
    running_.store(true, std::memory_order_relaxed);
    worker_ = std::thread(&LightbarBridge::workerLoop, this);
    cv_.notify_all();
    return true;
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
            const int tolerance = apex6_ ? kApex6ColorTolerance : 3;
            if (dr < tolerance && dg < tolerance && db < tolerance) {
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
    const auto kMinInterval = apex6_
        ? std::chrono::duration_cast<std::chrono::milliseconds>(kApex6MinInterval)
        : std::chrono::milliseconds(80);
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
            if (apex6_ && writes_.load(std::memory_order_relaxed) >= kApex6MaximumWrites) {
                running_.store(false, std::memory_order_relaxed);
                break;
            }
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
    if (apex6_) return writeApex6Color(r, g, b, error);
    constexpr std::size_t kHeaderSize = 20;
    auto loaded = workingConfig_;
    loaded[2] = 1;
    loaded[3] = 0;
    loaded[4] = 9;
    loaded[5] = 1;
    loaded[flydigi::kRgbBrightnessOffset] = brightness();
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

std::uint8_t LightbarBridge::brightness() const noexcept {
    return (std::min)(storedBrightness_, maxBrightness_);
}

std::uint8_t LightbarBridge::storedBrightness() const noexcept {
    return storedBrightness_;
}

std::uint8_t LightbarBridge::maxBrightness() const noexcept {
    return maxBrightness_;
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

std::filesystem::path LightbarBridge::apex6BackupPath() {
    std::filesystem::path base;
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, "LOCALAPPDATA") == 0 && value) {
        base = value;
        std::free(value);
    }
#else
    if (const char* home = std::getenv("HOME")) base = std::filesystem::path(home) / ".local" / "share";
#endif
    if (base.empty()) return {};
    return base / "ApexSenseBridge" / "apex6_led_backup.bin";
}

bool LightbarBridge::initializeApex6(std::string& error) {
    namespace apex6 = flydigi::apex6;
    apex6BackupFile_ = apex6BackupPath();
    if (apex6BackupFile_.empty()) {
        error = "Could not locate a folder for the Apex 6 Pro lighting backup.";
        return false;
    }
    std::vector<std::uint8_t> current;
    if (!device_.readApex6Ram(apex6::kRamLighting, current, error)) {
        error = "Could not read the Apex 6 Pro lighting configuration: " + error;
        return false;
    }
    if (!apex6::parseLightingRam(current)) {
        error = "The Apex 6 Pro lighting configuration has an unknown format; it was left untouched.";
        return false;
    }

    std::error_code fileError;
    if (std::filesystem::exists(apex6BackupFile_, fileError)) {
        std::vector<std::uint8_t> stale;
        {
            std::ifstream input(apex6BackupFile_, std::ios::binary);
            stale.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }
        if (stale.size() <= apex6::kRamMaximumSize && apex6::parseLightingRam(stale)) {
            if (stale != current) {
                if (!device_.writeApex6Ram(apex6::kRamLighting, stale, error)) {
                    error = "Could not restore the Apex 6 Pro lighting left by a previous session: " + error;
                    return false;
                }
                current = stale;
            }
        }
        std::filesystem::remove(apex6BackupFile_, fileError);
    }

    std::filesystem::create_directories(apex6BackupFile_.parent_path(), fileError);
    {
        std::ofstream output(apex6BackupFile_, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(current.data()),
                     static_cast<std::streamsize>(current.size()));
        if (!output) {
            error = "Could not save the Apex 6 Pro lighting backup; the lighting was left untouched.";
            return false;
        }
    }

    const auto config = apex6::parseLightingRam(current);
    apex6Backup_ = std::move(current);
    apex6Vibration_ = config && config->vibration;
    storedBrightness_ = (std::min)(
        static_cast<std::uint8_t>(config && config->brightness ? *config->brightness : 100),
        std::uint8_t{100});
    return true;
}

bool LightbarBridge::writeApex6Color(
    std::uint8_t r, std::uint8_t g, std::uint8_t b, std::string& error) {
    namespace apex6 = flydigi::apex6;
    const auto ram = apex6::buildSolidLightingRam(r, g, b, brightness(), apex6Vibration_);
    apex6Dirty_ = true;
    if (device_.writeApex6Ram(apex6::kRamLighting, ram, error)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    error.clear();
    return device_.writeApex6Ram(apex6::kRamLighting, ram, error);
}

void LightbarBridge::restoreApex6() {
    namespace apex6 = flydigi::apex6;
    std::error_code fileError;
    if (!apex6Dirty_) {
        std::filesystem::remove(apex6BackupFile_, fileError);
        return;
    }
    std::string error;
    for (int attempt = 0; attempt < 3; ++attempt) {
        error.clear();
        std::vector<std::uint8_t> readBack;
        if (device_.writeApex6Ram(apex6::kRamLighting, apex6Backup_, error) &&
            device_.readApex6Ram(apex6::kRamLighting, readBack, error) &&
            readBack == apex6Backup_) {
            std::filesystem::remove(apex6BackupFile_, fileError);
            return;
        }
        if (error.empty()) error = "The restored Apex 6 Pro lighting did not read back identically.";
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    recordFailure("Could not restore the Apex 6 Pro lighting (the backup stays in " +
                  apex6BackupFile_.string() + " and is restored by the next session): " + error);
}
} // namespace asb::dualsense
