#pragma once

#include "core/DeviceInfo.h"
#include "flydigi/Apex5Protocol.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asb::cli {

inline constexpr std::chrono::milliseconds kReattachPollInterval{500};
inline constexpr std::chrono::milliseconds kReattachNeutralKeepalive{100};

enum class PhysicalLossCause {
    InputDisconnected,
    InputStale,
    InputFailed,
    FeedbackFailed,
};

[[nodiscard]] constexpr std::string_view describePhysicalLossCause(
    PhysicalLossCause cause) noexcept {
    switch (cause) {
    case PhysicalLossCause::InputDisconnected: return "physical input disconnected";
    case PhysicalLossCause::InputStale: return "physical input silent";
    case PhysicalLossCause::InputFailed: return "physical input failed";
    case PhysicalLossCause::FeedbackFailed: return "feedback write failed";
    }
    return "unknown";
}

class PhysicalReattachBudget {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto kWindow = std::chrono::minutes(10);
    static constexpr std::size_t kMaximumPerWindow = 6;

    [[nodiscard]] bool allows(Clock::time_point now = Clock::now()) const noexcept {
        return recent(now) < kMaximumPerWindow;
    }

    void record(Clock::time_point now = Clock::now()) noexcept {
        losses_[next_] = now;
        next_ = (next_ + 1) % kMaximumPerWindow;
    }

    [[nodiscard]] std::size_t recent(Clock::time_point now = Clock::now()) const noexcept {
        std::size_t count = 0;
        for (const auto& at : losses_) {
            if (at && now - *at < kWindow) ++count;
        }
        return count;
    }

private:
    std::array<std::optional<Clock::time_point>, kMaximumPerWindow> losses_{};
    std::size_t next_ = 0;
};

namespace detail {

[[nodiscard]] inline bool equalsIgnoreCase(const std::wstring& left,
                                           const std::wstring& right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto a = left[index];
        const auto b = right[index];
        if (a == b) continue;
        if (a < 128 && b < 128 &&
            std::tolower(static_cast<unsigned char>(a)) ==
                std::tolower(static_cast<unsigned char>(b))) {
            continue;
        }
        return false;
    }
    return true;
}

[[nodiscard]] inline bool sameModel(const HidDeviceInfo& original,
                                    const HidDeviceInfo& candidate) noexcept {
    return candidate.vendorId == original.vendorId &&
           candidate.productId == original.productId &&
           candidate.usagePage == original.usagePage;
}

}

[[nodiscard]] inline std::optional<std::size_t> selectReturningController(
    const HidDeviceInfo& original,
    const std::vector<HidDeviceInfo>& candidates,
    bool allowSingleModelFallback) {
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!original.path.empty() &&
            detail::equalsIgnoreCase(candidates[index].path, original.path)) {
            return index;
        }
    }
    if (!original.serial.empty()) {
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            if (detail::sameModel(original, candidates[index]) &&
                candidates[index].serial == original.serial) {
                return index;
            }
        }
    }
    if (!original.containerId.empty()) {
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            if (detail::sameModel(original, candidates[index]) &&
                detail::equalsIgnoreCase(candidates[index].containerId, original.containerId)) {
                return index;
            }
        }
    }
    if (!allowSingleModelFallback) return std::nullopt;
    std::optional<std::size_t> only;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!detail::sameModel(original, candidates[index])) continue;
        if (only) return std::nullopt;
        only = index;
    }
    return only;
}

struct PhysicalReattachSteps {
    bool applyProfile = false;
    bool setInputTransport = false;

    bool operator==(const PhysicalReattachSteps&) const = default;
};

[[nodiscard]] inline PhysicalReattachSteps planPhysicalReattach(
    std::optional<std::uint8_t> requestedProfileSlot,
    const std::optional<flydigi::ProfileStatus>& profile,
    const std::optional<flydigi::InputTransportStatus>& transport) noexcept {
    PhysicalReattachSteps steps;
    steps.applyProfile = requestedProfileSlot &&
        (!profile || profile->switchBank || profile->slot != *requestedProfileSlot);
    steps.setInputTransport = !transport || !transport->controllerData || !transport->rawData;
    return steps;
}

[[nodiscard]] inline bool sameControllerFamily(bool originalApex4, bool originalApex5,
                                               bool returnedApex4, bool returnedApex5) noexcept {
    return originalApex4 == returnedApex4 && originalApex5 == returnedApex5;
}

}
