#include "platform/CompetingHapticsEngines.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace asb::platform {
namespace {

struct KnownEngine {
    std::wstring_view executable;
    std::string_view name;
};

constexpr std::array<KnownEngine, 3> kKnownEngines{{
    {L"xHaptics Engine.exe", "xHaptics Engine.exe"},
    {L"XHaptics.exe", "XHaptics.exe"},
    {L"x-haptic-engine-fusion.exe", "x-haptic-engine-fusion.exe"},
}};

wchar_t lowerAscii(wchar_t value) noexcept {
    return value >= L'A' && value <= L'Z' ? static_cast<wchar_t>(value - L'A' + L'a') : value;
}

bool equalsIgnoreCase(std::wstring_view left, std::wstring_view right) noexcept {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(),
                      [](wchar_t a, wchar_t b) { return lowerAscii(a) == lowerAscii(b); });
}

} // namespace

std::vector<std::string> findCompetingHapticsEngines(
    const std::vector<std::wstring>& runningExecutables) {
    std::vector<std::string> found;
    for (const auto& engine : kKnownEngines) {
        const bool running = std::any_of(
            runningExecutables.begin(), runningExecutables.end(),
            [&engine](const std::wstring& name) {
                return equalsIgnoreCase(name, engine.executable);
            });
        if (running) found.emplace_back(engine.name);
    }
    return found;
}

std::string competingHapticsEnginesWarning(const std::vector<std::string>& engines) {
    if (engines.empty()) return {};
    std::string joined;
    for (const auto& engine : engines) {
        if (!joined.empty()) joined += ", ";
        joined += engine;
    }
    return "WARNING: Flydigi X-Haptics is running (" + joined +
           "). It can send haptic frames to the same controller and compete with "
           "ApexSenseBridge. Close it before a bridge session if vibration or triggers "
           "behave erratically.";
}

} // namespace asb::platform
