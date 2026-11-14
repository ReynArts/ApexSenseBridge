#include "flydigi/Apex6Lighting.h"

#include <algorithm>

namespace asb::flydigi::apex6 {
namespace {

constexpr std::array<std::uint8_t, 4> kLightingMagic{'L', 'G', 'H', 'T'};
constexpr std::uint16_t kLightingVersion = 2;
constexpr std::size_t kLightingHeaderSize = 12;
constexpr std::size_t kEnvelopeSize = 4;

void put16(std::vector<std::uint8_t>& out, unsigned value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }
}

std::uint16_t read16(std::span<const std::uint8_t> data, std::size_t at) noexcept {
    return static_cast<std::uint16_t>(data[at] | (data[at + 1] << 8));
}

std::uint32_t read32(std::span<const std::uint8_t> data, std::size_t at) noexcept {
    return static_cast<std::uint32_t>(data[at]) |
           (static_cast<std::uint32_t>(data[at + 1]) << 8) |
           (static_cast<std::uint32_t>(data[at + 2]) << 16) |
           (static_cast<std::uint32_t>(data[at + 3]) << 24);
}

std::uint16_t sum16(std::span<const std::uint8_t> data) noexcept {
    unsigned total = 0;
    for (const auto byte : data) total += byte;
    return static_cast<std::uint16_t>(total & 0xFFFF);
}

std::span<const std::uint8_t> frameOf(std::span<const std::uint8_t> report) noexcept {
    if (report.size() >= kReportSize && report[0] == kReportId &&
        report[1] == kMagic0 && report[2] == kMagic1) {
        return report.subspan(1, kFrameSize);
    }
    if (report.size() >= kFrameSize && report[0] == kMagic0 && report[1] == kMagic1) {
        return report.first(kFrameSize);
    }
    return {};
}

} // namespace

std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const auto byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return ~crc;
}

std::uint16_t crc16Ccitt(std::span<const std::uint8_t> data) noexcept {
    std::uint16_t crc = 0xFFFF;
    for (const auto byte : data) {
        crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(byte) << 8));
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;
}

std::vector<std::uint8_t> buildSolidLightingRam(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue,
    std::uint8_t brightness, bool vibration) {
    const std::array<std::uint8_t, 6> data{
        kLightingSolid, red, green, blue, brightness,
        static_cast<std::uint8_t>(vibration ? 1 : 0)};
    std::vector<std::uint8_t> bin(kLightingMagic.begin(), kLightingMagic.end());
    put16(bin, kLightingVersion);
    put32(bin, static_cast<std::uint32_t>(data.size()));
    put16(bin, sum16(bin));
    bin.insert(bin.end(), data.begin(), data.end());
    put16(bin, sum16(data));

    std::vector<std::uint8_t> ram;
    put16(ram, static_cast<unsigned>(bin.size() + kEnvelopeSize));
    put16(ram, crc16Ccitt(bin));
    ram.insert(ram.end(), bin.begin(), bin.end());
    return ram;
}

std::optional<LightingConfig> parseLightingRam(
    std::span<const std::uint8_t> ram) noexcept {
    if (ram.size() < kEnvelopeSize + kLightingHeaderSize + 2) return std::nullopt;
    if (read16(ram, 0) != ram.size()) return std::nullopt;
    const auto bin = ram.subspan(kEnvelopeSize);
    if (read16(ram, 2) != crc16Ccitt(bin)) return std::nullopt;
    if (!std::equal(kLightingMagic.begin(), kLightingMagic.end(), bin.begin())) {
        return std::nullopt;
    }
    if (read16(bin, 10) != sum16(bin.first(10))) return std::nullopt;
    const std::size_t size = read32(bin, 6);
    if (size == 0 || size > bin.size() || bin.size() < kLightingHeaderSize + size + 2) {
        return std::nullopt;
    }
    const auto data = bin.subspan(kLightingHeaderSize, size);
    if (read16(bin, kLightingHeaderSize + size) != sum16(data)) return std::nullopt;

    LightingConfig config;
    config.mode = data[0];
    std::size_t next = 1;
    switch (config.mode) {
    case kLightingOff:
        break;
    case kLightingSolid:
        if (data.size() < 5) return std::nullopt;
        config.brightness = data[4];
        next = 5;
        break;
    case kLightingBreathing:
    case kLightingGradient: {
        if (data.size() < 4) return std::nullopt;
        config.brightness = data[1];
        const std::size_t colors = data[3];
        next = 4 + colors * 3;
        if (next > data.size()) return std::nullopt;
        break;
    }
    case kLightingStreaming:
    case kLightingDefault:
        if (data.size() < 3) return std::nullopt;
        config.brightness = data[1];
        next = 3;
        break;
    default:
        return std::nullopt;
    }
    config.vibration = next < data.size() && (data[next] & 1) != 0;
    return config;
}

Report buildRamInfoRequest(std::uint8_t ramId) {
    const std::array<std::uint8_t, 2> payload{1, ramId};
    return buildCommand(kCmdRamRead, payload);
}

Report buildRamReadRequest(std::uint8_t ramId, std::uint8_t index) {
    const std::array<std::uint8_t, 4> payload{
        0, ramId, index, static_cast<std::uint8_t>(kRamChunkSize)};
    return buildCommand(kCmdRamRead, payload);
}

Report buildRamWriteBegin(std::uint8_t ramId, std::uint16_t length, std::uint32_t crc) {
    std::vector<std::uint8_t> payload{0, ramId};
    put16(payload, length);
    put32(payload, crc);
    return buildCommand(kCmdRamWrite, payload);
}

Report buildRamWriteChunk(std::uint16_t offset, std::span<const std::uint8_t> data) {
    const auto size = (std::min)(data.size(), std::size_t{23});
    std::vector<std::uint8_t> payload{1};
    put16(payload, offset);
    payload.push_back(static_cast<std::uint8_t>(size));
    payload.insert(payload.end(), data.begin(), data.begin() + size);
    return buildCommand(kCmdRamWrite, payload);
}

Report buildRamWriteCommit(std::uint16_t length, std::uint32_t crc) {
    std::vector<std::uint8_t> payload{2};
    put16(payload, length);
    put32(payload, crc);
    return buildCommand(kCmdRamWrite, payload);
}

Report buildRamWriteAbort() {
    const std::array<std::uint8_t, 1> payload{3};
    return buildCommand(kCmdRamWrite, payload);
}

std::optional<std::vector<std::uint8_t>> ramReplyPayload(
    std::span<const std::uint8_t> report, std::uint8_t command,
    std::uint8_t mode) noexcept {
    const auto frame = frameOf(report);
    if (frame.size() != kFrameSize || frame[2] != command ||
        frame[31] != checksum(frame) || frame[3] != mode) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(frame.begin() + 3, frame.begin() + 31);
}

std::optional<RamInfo> parseRamInfo(std::span<const std::uint8_t> payload) noexcept {
    if (payload.size() < 10) return std::nullopt;
    return RamInfo{payload[1], payload[2], read16(payload, 4), read32(payload, 6)};
}

std::optional<RamBlock> parseRamBlock(std::span<const std::uint8_t> payload) noexcept {
    if (payload.size() < 8) return std::nullopt;
    const std::size_t size = (std::min)(static_cast<std::size_t>(payload[7]),
                                        payload.size() - 8);
    RamBlock block;
    block.status = payload[1];
    block.ramId = payload[2];
    block.index = payload[6];
    block.data.assign(payload.begin() + 8, payload.begin() + 8 + size);
    return block;
}

std::optional<RamCommit> parseRamCommit(std::span<const std::uint8_t> payload) noexcept {
    if (payload.size() < 10) return std::nullopt;
    return RamCommit{payload[1], read16(payload, 4), read32(payload, 6)};
}

} // namespace asb::flydigi::apex6
