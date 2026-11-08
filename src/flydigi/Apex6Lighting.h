#pragma once

#include "flydigi/Apex6Protocol.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace asb::flydigi::apex6 {

inline constexpr std::uint8_t kCmdRamRead = 0xA3;
inline constexpr std::uint8_t kCmdRamWrite = 0xA4;
inline constexpr std::uint8_t kRamLighting = 4;
inline constexpr std::size_t kRamChunkSize = 16;
inline constexpr std::size_t kRamMaximumSize = 4096;

inline constexpr std::uint8_t kLightingOff = 0;
inline constexpr std::uint8_t kLightingSolid = 1;
inline constexpr std::uint8_t kLightingBreathing = 2;
inline constexpr std::uint8_t kLightingGradient = 3;
inline constexpr std::uint8_t kLightingStreaming = 4;
inline constexpr std::uint8_t kLightingDefault = 5;

struct LightingConfig {
    std::uint8_t mode = kLightingOff;
    std::optional<std::uint8_t> brightness;
    bool vibration = false;
};

struct RamInfo {
    std::uint8_t status = 0xFF;
    std::uint8_t ramId = 0;
    std::uint16_t length = 0;
    std::uint32_t crc32 = 0;
};

struct RamBlock {
    std::uint8_t status = 0xFF;
    std::uint8_t ramId = 0;
    std::uint8_t index = 0;
    std::vector<std::uint8_t> data;
};

struct RamCommit {
    std::uint8_t status = 0xFF;
    std::uint16_t length = 0;
    std::uint32_t crc32 = 0;
};

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept;
[[nodiscard]] std::uint16_t crc16Ccitt(std::span<const std::uint8_t> data) noexcept;

[[nodiscard]] std::vector<std::uint8_t> buildSolidLightingRam(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue,
    std::uint8_t brightness, bool vibration);
[[nodiscard]] std::optional<LightingConfig> parseLightingRam(
    std::span<const std::uint8_t> ram) noexcept;

[[nodiscard]] Report buildRamInfoRequest(std::uint8_t ramId);
[[nodiscard]] Report buildRamReadRequest(std::uint8_t ramId, std::uint8_t index);
[[nodiscard]] Report buildRamWriteBegin(std::uint8_t ramId, std::uint16_t length,
                                        std::uint32_t crc);
[[nodiscard]] Report buildRamWriteChunk(std::uint16_t offset,
                                        std::span<const std::uint8_t> data);
[[nodiscard]] Report buildRamWriteCommit(std::uint16_t length, std::uint32_t crc);
[[nodiscard]] Report buildRamWriteAbort();

[[nodiscard]] std::optional<std::vector<std::uint8_t>> ramReplyPayload(
    std::span<const std::uint8_t> report, std::uint8_t command,
    std::uint8_t mode) noexcept;
[[nodiscard]] std::optional<RamInfo> parseRamInfo(
    std::span<const std::uint8_t> payload) noexcept;
[[nodiscard]] std::optional<RamBlock> parseRamBlock(
    std::span<const std::uint8_t> payload) noexcept;
[[nodiscard]] std::optional<RamCommit> parseRamCommit(
    std::span<const std::uint8_t> payload) noexcept;

} // namespace asb::flydigi::apex6
