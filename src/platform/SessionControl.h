#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace asb::platform {

inline constexpr std::uint32_t kSessionStatusMagic = 0x53425341; // "ASBS"
inline constexpr std::uint16_t kSessionProtocolVersion = 1;
inline constexpr std::size_t kSessionStatusSize = 512;
inline constexpr std::size_t kSessionMessageCapacity = 496;

enum class SessionPhase : std::uint16_t {
    Empty = 0,
    Starting = 1,
    Ready = 2,
    Stopping = 3,
    Stopped = 4,
    Failed = 5,
};

struct SessionStatusBlock {
    std::uint32_t magic = kSessionStatusMagic;
    std::uint16_t protocolVersion = kSessionProtocolVersion;
    std::uint16_t phase = static_cast<std::uint16_t>(SessionPhase::Empty);
    std::int32_t exitCode = 0;
    std::uint32_t messageLength = 0;
    std::array<char, kSessionMessageCapacity> message{};
};
static_assert(sizeof(SessionStatusBlock) == kSessionStatusSize);

[[nodiscard]] bool isValidSessionToken(std::string_view token) noexcept;
[[nodiscard]] std::string sessionReadyEventName(std::string_view token);
[[nodiscard]] std::string sessionStopEventName(std::string_view token);
[[nodiscard]] std::string sessionStatusMappingName(std::string_view token);

class SessionControl {
public:
    virtual ~SessionControl() = default;

    virtual bool publish(SessionPhase phase,
                         int exitCode,
                         std::string_view message,
                         std::string& error) noexcept = 0;
    virtual bool signalReady(std::string& error) noexcept = 0;
    [[nodiscard]] virtual bool stopRequested() const noexcept = 0;
};

// Machine maintenance (uninstall/repair) needs to ask an active bridge to run
// its normal neutralize/detach/restore path before a forced process fallback.
// This signal is intentionally separate from the versioned Playnite IPC.
class GlobalSessionStop {
public:
    virtual ~GlobalSessionStop() = default;
    [[nodiscard]] virtual bool stopRequested() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<GlobalSessionStop> createGlobalSessionStop(
    std::string& error);
[[nodiscard]] bool requestGlobalSessionStop(
    std::chrono::milliseconds timeout, std::string& error) noexcept;

// The Playnite-side owner creates all three named objects before launching the
// bridge. The bridge only opens and signals them; it never widens access rights.
[[nodiscard]] std::unique_ptr<SessionControl> connectSessionControl(
    std::string_view token, std::string& error);

} // namespace asb::platform
