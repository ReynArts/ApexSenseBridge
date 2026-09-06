#include "core/ApexProfileRestoreGuard.h"

#include "flydigi/Apex5Protocol.h"

#include <chrono>
#include <sstream>
#include <thread>

namespace asb {

ApexProfileRestoreGuard::ApexProfileRestoreGuard(
    flydigi::Apex5Device& device, std::uint8_t originalSlot) noexcept
    : device_(device), originalSlot_(originalSlot) {}

ApexProfileRestoreGuard::~ApexProfileRestoreGuard() {
    std::string ignored;
    (void)restore(ignored);
}

bool ApexProfileRestoreGuard::restore(std::string& error) noexcept {
    if (!armed_) return true;

    try {
        std::string lastError;
        for (int attempt = 1; attempt <= 3; ++attempt) {
            std::string applyError;
            const bool acknowledged =
                device_.applyProfile(originalSlot_, applyError);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            flydigi::ProfileStatus status{};
            std::string statusError;
            const bool statusRead = device_.readProfileStatus(status, statusError);
            if (statusRead && !status.switchBank && status.slot == originalSlot_) {
                armed_ = false;
                error.clear();
                return true;
            }

            std::ostringstream detail;
            detail << "profile restore attempt " << attempt << " failed";
            if (!acknowledged && !applyError.empty()) {
                detail << ": " << applyError;
            } else if (!statusRead && !statusError.empty()) {
                detail << ": " << statusError;
            } else if (statusRead) {
                detail << ": controller reported raw slot "
                       << static_cast<unsigned int>(status.rawSlot);
            }
            lastError = detail.str();
        }
        error = lastError;
        return false;
    } catch (...) {
        error = "Unexpected failure while restoring the original Apex 5 profile";
        return false;
    }
}

} // namespace asb
