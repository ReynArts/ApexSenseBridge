#ifdef NDEBUG
#undef NDEBUG
#endif

#include "flydigi/Apex6Lighting.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    using namespace asb::flydigi::apex6;

    const std::vector<std::uint8_t> original{
        0x20, 0x00, 0x52, 0x40, 0x4C, 0x47, 0x48, 0x54, 0x02, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x3F, 0x01,
        0x03, 0x50, 0x32, 0x03, 0x6D, 0x00, 0xFF, 0x30, 0x80, 0xFF, 0x09, 0x00, 0xFF, 0x00, 0xAB, 0x04};
    assert(crc32(original) == 0x9164741Cu);
    assert(crc16Ccitt(std::span<const std::uint8_t>(original).subspan(4)) == 0x4052);
    const auto parsed = parseLightingRam(original);
    assert(parsed && parsed->mode == kLightingGradient);
    assert(parsed->brightness && *parsed->brightness == 80);
    assert(!parsed->vibration);

    const auto green = buildSolidLightingRam(0, 255, 0, 80, false);
    const std::vector<std::uint8_t> expectedGreen{
        0x18, 0x00, 0x33, 0x9D, 0x4C, 0x47, 0x48, 0x54, 0x02, 0x00, 0x06, 0x00, 0x00, 0x00, 0x37, 0x01,
        0x01, 0x00, 0xFF, 0x00, 0x50, 0x00, 0x50, 0x01};
    assert(green == expectedGreen);
    assert(crc32(green) == 0x7EE3282Cu);
    const auto solid = parseLightingRam(green);
    assert(solid && solid->mode == kLightingSolid && solid->brightness && *solid->brightness == 80);
    assert(!solid->vibration);
    const auto withVibration = parseLightingRam(buildSolidLightingRam(1, 2, 3, 40, true));
    assert(withVibration && withVibration->vibration && *withVibration->brightness == 40);

    auto broken = original;
    broken[20] ^= 0x01;
    assert(!parseLightingRam(broken));
    broken = original;
    broken[2] ^= 0x01;
    assert(!parseLightingRam(broken));
    assert(!parseLightingRam(std::vector<std::uint8_t>(original.begin(), original.end() - 1)));
    assert(!parseLightingRam({}));

    const std::vector<std::uint8_t> off = [] {
        auto ram = buildSolidLightingRam(0, 0, 0, 0, false);
        return ram;
    }();
    assert(parseLightingRam(off)->mode == kLightingSolid);

    const auto info = buildRamInfoRequest(kRamLighting);
    assert(info[3] == kCmdRamRead && info[4] == 4 && info[5] == 1 && info[6] == kRamLighting);
    assert(isValidFrame(info, kCmdRamRead));
    const auto block = buildRamReadRequest(kRamLighting, 3);
    assert(block[5] == 0 && block[6] == kRamLighting && block[7] == 3 && block[8] == 16);
    const auto begin = buildRamWriteBegin(kRamLighting, 24, 0x7EE3282Cu);
    assert(begin[3] == kCmdRamWrite && begin[5] == 0 && begin[6] == kRamLighting);
    assert(begin[7] == 24 && begin[8] == 0);
    assert(begin[9] == 0x2C && begin[10] == 0x28 && begin[11] == 0xE3 && begin[12] == 0x7E);
    assert(isValidFrame(begin, kCmdRamWrite));
    const std::array<std::uint8_t, 8> tail{1, 2, 3, 4, 5, 6, 7, 8};
    const auto chunk = buildRamWriteChunk(16, tail);
    assert(chunk[5] == 1 && chunk[6] == 16 && chunk[7] == 0 && chunk[8] == 8 && chunk[9] == 1 && chunk[16] == 8);
    const auto commit = buildRamWriteCommit(24, 0x7EE3282Cu);
    assert(commit[5] == 2 && commit[6] == 24 && commit[7] == 0 && commit[8] == 0x2C && commit[11] == 0x7E);
    const auto abort = buildRamWriteAbort();
    assert(abort[4] == 3 && abort[5] == 3);

    const std::array<std::uint8_t, 32> infoReply{
        0x5A, 0xA5, 0xA3, 0x01, 0x00, 0x04, 0x00, 0x20, 0x00, 0x1C, 0x74, 0x64, 0x91, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4D};
    const auto infoPayload = ramReplyPayload(infoReply, kCmdRamRead, 1);
    assert(infoPayload);
    assert(!ramReplyPayload(infoReply, kCmdRamRead, 0));
    assert(!ramReplyPayload(infoReply, kCmdRamWrite, 1));
    const auto parsedInfo = parseRamInfo(*infoPayload);
    assert(parsedInfo && parsedInfo->status == 0 && parsedInfo->ramId == 4);
    assert(parsedInfo->length == 32 && parsedInfo->crc32 == 0x9164741Cu);

    std::array<std::uint8_t, 33> prefixed{};
    std::copy(infoReply.begin(), infoReply.end(), prefixed.begin() + 1);
    assert(ramReplyPayload(prefixed, kCmdRamRead, 1));
    auto badChecksum = infoReply;
    badChecksum[31] ^= 0xFF;
    assert(!ramReplyPayload(badChecksum, kCmdRamRead, 1));

    const std::array<std::uint8_t, 32> blockReply{
        0x5A, 0xA5, 0xA3, 0x00, 0x00, 0x04, 0x00, 0x20, 0x00, 0x00, 0x10, 0x20, 0x00, 0x52, 0x40, 0x4C,
        0x47, 0x48, 0x54, 0x02, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x3F, 0x01, 0x00, 0x00, 0x00, 0x00, 0x08};
    const auto blockPayload = ramReplyPayload(blockReply, kCmdRamRead, 0);
    assert(blockPayload);
    const auto parsedBlock = parseRamBlock(*blockPayload);
    assert(parsedBlock && parsedBlock->status == 0 && parsedBlock->ramId == 4 && parsedBlock->index == 0);
    assert(parsedBlock->data.size() == 16 && parsedBlock->data[0] == 0x20 && parsedBlock->data[15] == 0x01);

    const std::vector<std::uint8_t> commitPayload{2, 0, 4, 0, 24, 0, 0x2C, 0x28, 0xE3, 0x7E};
    const auto parsedCommit = parseRamCommit(commitPayload);
    assert(parsedCommit && parsedCommit->status == 0 && parsedCommit->length == 24);
    assert(parsedCommit->crc32 == 0x7EE3282Cu);
    assert(!parseRamCommit(std::vector<std::uint8_t>{2, 0}));
    return 0;
}
