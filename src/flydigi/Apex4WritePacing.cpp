#include "flydigi/Apex4WritePacing.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>

namespace asb::flydigi {
namespace {

std::string readVariable(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) return {};
    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

}

Apex4RumbleSpacing parseApex4RumbleSpacing(const char* text) noexcept {
    if (text == nullptr) return {};
    const char* first = text;
    const char* last = text + std::strlen(text);
    while (first < last && (*first == ' ' || *first == '\t')) ++first;
    while (last > first && (last[-1] == ' ' || last[-1] == '\t' ||
                            last[-1] == '\r' || last[-1] == '\n')) {
        --last;
    }
    if (first == last) return {};
    long long value = 0;
    const auto [end, status] = std::from_chars(first, last, value);
    if (status == std::errc::result_out_of_range) {
        value = *first == '-' ? 0 : kApex4MaximumWriteSpacing.count();
    } else if (status != std::errc() || end != last) {
        return {};
    }
    const auto clamped = std::clamp<long long>(
        value, kApex4MinimumWriteSpacing.count(), kApex4MaximumWriteSpacing.count());
    return {std::chrono::milliseconds(clamped), true};
}

Apex4RumbleSpacing configuredApex4RumbleSpacing() noexcept {
    static const Apex4RumbleSpacing configured = [] {
        try {
            const auto value = readVariable(kApex4RumbleSpacingVariable);
            return parseApex4RumbleSpacing(value.empty() ? nullptr : value.c_str());
        } catch (...) {
            return Apex4RumbleSpacing{};
        }
    }();
    return configured;
}

std::chrono::milliseconds apex4WriteSpacing(Apex4WriteKind previous,
                                            Apex4WriteKind next,
                                            std::chrono::milliseconds rumbleSpacing) noexcept {
    if (previous == Apex4WriteKind::Rumble && next == Apex4WriteKind::Rumble) {
        return std::clamp(rumbleSpacing, kApex4MinimumWriteSpacing, kApex4MaximumWriteSpacing);
    }
    return kApex4TriggerWriteSpacing;
}

Apex4WritePacer::Apex4WritePacer(std::chrono::milliseconds rumbleSpacing) noexcept
    : rumbleSpacing_(std::clamp(rumbleSpacing, kApex4MinimumWriteSpacing,
                                kApex4MaximumWriteSpacing)) {}

Apex4WritePacer::Clock::time_point Apex4WritePacer::earliest(Apex4WriteKind next) const noexcept {
    if (lastKind_ == Apex4WriteKind::None) return {};
    return lastWriteAt_ + apex4WriteSpacing(lastKind_, next, rumbleSpacing_);
}

void Apex4WritePacer::record(Apex4WriteKind kind, Clock::time_point at) noexcept {
    lastKind_ = kind;
    lastWriteAt_ = at;
}

} // namespace asb::flydigi
