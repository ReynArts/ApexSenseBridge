#ifdef NDEBUG
#undef NDEBUG
#endif

#include "flydigi/Apex6Imu.h"
#include "flydigi/Apex6Protocol.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace asb::flydigi::apex6;

std::vector<std::uint8_t> fromHex(std::string_view hex) {
    std::vector<std::uint8_t> bytes;
    for (std::size_t index = 0; index + 1 < hex.size(); index += 2) {
        const auto nibble = [](char c) {
            return static_cast<std::uint8_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
        };
        bytes.push_back(static_cast<std::uint8_t>(
            (nibble(hex[index]) << 4U) | nibble(hex[index + 1])));
    }
    return bytes;
}

void putWord(std::vector<std::uint8_t>& report, std::size_t frameOffset,
             std::int16_t value) {
    const auto raw = static_cast<std::uint16_t>(value);
    report[1 + frameOffset] = static_cast<std::uint8_t>(raw & 0xFF);
    report[2 + frameOffset] = static_cast<std::uint8_t>(raw >> 8U);
}

std::vector<std::uint8_t> operatorReport(const RawMotion& motion,
                                         const OperatorTriggers& triggers = {}) {
    std::vector<std::uint8_t> report(kReportSize, 0);
    report[1] = kMagic0;
    report[2] = kMagic1;
    report[3] = kCmdOperatorData;
    report[1 + kOperatorLeftTriggerOffset] = triggers.left;
    report[1 + kOperatorRightTriggerOffset] = triggers.right;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        putWord(report, kOperatorGyroOffset + axis * 2, motion.gyro[axis]);
        putWord(report, kOperatorAccelOffset + axis * 2, motion.accel[axis]);
    }
    report[32] = checksum(std::span<const std::uint8_t>(report).subspan(1));
    return report;
}

} // namespace

int main() {
    const auto info = fromHex(
        "005aa50101009601000000000845010090500509010900000145000010509f0024");
    const auto parsedInfo = parseDeviceInfo(info);
    assert(parsedInfo && parsedInfo->deviceType == 0x96);
    assert(parsedInfo->features == 0x9F && hasImu(parsedInfo->features));
    assert(!hasImu(0x9E));
    const auto gyroType = parseGyroChipType(info);
    assert(gyroType && *gyroType == 1);
    auto corruptedInfo = info;
    corruptedInfo[32] ^= 0x01;
    assert(!parseGyroChipType(corruptedInfo));

    const auto transport = fromHex(
        "005aa5100100000000000000000000000000000000000000000000000000000011");
    const auto status = parseTransportStatus(transport);
    assert(status && *status == TransportStatus{});
    assert(!status->takeoverActive() && !status->allowThirdParty());
    auto takeover = transport;
    takeover[6] = 0x01;
    takeover[9] = 0x01;
    takeover[10] = 0x01;
    takeover[32] = static_cast<std::uint8_t>(takeover[32] + 3);
    const auto taken = parseTransportStatus(takeover);
    assert(taken && taken->takeoverActive() && taken->allowThirdParty());
    auto errorReply = transport;
    errorReply[4] = 0x00;
    errorReply[32] = 0x10;
    assert(!parseTransportStatus(errorReply));
    assert(!parseTransportStatus(info));

    const auto readRequest = buildReadTransport();
    assert(readRequest[3] == 0x10 && readRequest[4] == 0x02 && readRequest[32] == 0x12);

    const auto enable = buildSetRawDataRouting(true);
    assert(enable[0] == 0x00 && enable[1] == 0x5A && enable[2] == 0xA5);
    assert(enable[3] == 0x11 && enable[4] == 29);
    assert(enable[5] == 0xFF && enable[6] == 0x01);
    assert(enable[7] == 0xFF && enable[8] == 0xFF && enable[9] == 0xFF);
    for (std::size_t index = 10; index < 32; ++index) assert(enable[index] == 0);
    assert(enable[32] == 0x2B);
    assert(isValidFrame(enable, kCmdSetTransport));
    const auto disable = buildSetRawDataRouting(false);
    assert(disable[5] == 0xFF && disable[6] == 0x00);
    assert(disable[32] == 0x2A);

    RawMotion expected{};
    expected.gyro = {120, -340, 560};
    expected.accel = {-30, 4096, 17};
    RawMotion decoded{};
    const auto report = operatorReport(expected);
    assert(parseOperatorReport(report, decoded) == OperatorParse::Motion);
    assert(decoded == expected);

    const std::span<const std::uint8_t> bareFrame(report.data() + 1, kFrameSize);
    RawMotion fromFrame{};
    assert(parseOperatorReport(bareFrame, fromFrame) == OperatorParse::Motion);
    assert(fromFrame == expected);

    OperatorTriggers expectedTriggers{};
    expectedTriggers.left = 201;
    expectedTriggers.right = 37;
    RawMotion triggerMotion{};
    OperatorTriggers decodedTriggers{};
    assert(parseOperatorReport(operatorReport(expected, expectedTriggers), triggerMotion,
                               decodedTriggers) == OperatorParse::Motion);
    assert(decodedTriggers == expectedTriggers);
    assert(triggerMotion == expected);

    OperatorTriggers bothPressed{};
    bothPressed.left = 255;
    bothPressed.right = 255;
    OperatorTriggers decodedBoth{};
    assert(parseOperatorReport(operatorReport(expected, bothPressed), triggerMotion,
                               decodedBoth) == OperatorParse::Motion);
    assert(decodedBoth == bothPressed);

    OperatorTriggers untouchedTriggers{};
    untouchedTriggers.left = 9;
    untouchedTriggers.right = 9;
    auto corruptedTriggers = operatorReport(expected, expectedTriggers);
    corruptedTriggers[32] ^= 0x08;
    assert(parseOperatorReport(corruptedTriggers, triggerMotion, untouchedTriggers) ==
           OperatorParse::InvalidChecksum);
    assert(untouchedTriggers.left == 9 && untouchedTriggers.right == 9);

    RawMotion untouched{};
    untouched.gyro = {7, 7, 7};
    auto badChecksum = report;
    badChecksum[32] ^= 0x40;
    assert(parseOperatorReport(badChecksum, untouched) == OperatorParse::InvalidChecksum);
    assert(untouched.gyro[0] == 7);
    assert(parseOperatorReport(info, untouched) == OperatorParse::NotOperatorFrame);
    assert(parseOperatorReport(std::span<const std::uint8_t>(report).first(20), untouched) ==
           OperatorParse::NotOperatorFrame);
    assert(parseOperatorReport({}, untouched) == OperatorParse::NotOperatorFrame);

    RawMotion extremes{};
    extremes.gyro = {-32768, -32768, 32767};
    extremes.accel = {-32768, 32767, -32768};
    RawMotion decodedExtremes{};
    assert(parseOperatorReport(operatorReport(extremes), decodedExtremes) ==
           OperatorParse::Motion);
    assert(decodedExtremes == extremes);

    RawMotion rest{};
    rest.gyro = {3, -2, 1};
    rest.accel = {0, 0, 4500};
    const auto converted = convertMotion(rest);
    assert(converted.gyroX == -2);
    assert(converted.gyroY == -3);
    assert(converted.gyroZ == -1);
    assert(converted.accelX == 0);
    assert(converted.accelY == 9459);
    assert(converted.accelZ == 0);

    RawMotion lying{};
    lying.accel = {0, -4500, 0};
    assert(convertMotion(lying).accelZ == 9459);

    RawMotion oneG{};
    oneG.accel = {0, 0, 4758};
    const auto oneGConverted = convertMotion(oneG);
    assert(oneGConverted.accelY >= 9995 && oneGConverted.accelY <= 10005);

    RawMotion saturated{};
    saturated.gyro = {-32768, -32768, -32768};
    saturated.accel = {32767, -32768, -32768};
    const auto clamped = convertMotion(saturated);
    assert(clamped.gyroX == -32768);
    assert(clamped.gyroY == 32767);
    assert(clamped.gyroZ == 32767);
    assert(clamped.accelX == 32767);
    assert(clamped.accelY == -32768);
    assert(clamped.accelZ == 32767);

    MotionCalibration identity;
    identity.gyroSource = {0, 1, 2};
    identity.gyroSign = {1, 1, 1};
    identity.accelSource = {0, 1, 2};
    identity.accelSign = {1, 1, 1};
    identity.accelNumerator = 1;
    identity.accelDenominator = 1;
    RawMotion sample{};
    sample.gyro = {10, 20, 30};
    sample.accel = {40, 50, 60};
    const auto passthrough = convertMotion(sample, identity);
    assert(passthrough == MotionSample({10, 20, 30, 40, 50, 60}));

    MotionCalibration outOfRange;
    outOfRange.gyroSource = {9, 4, 5};
    const auto wrapped = convertMotion(sample, outOfRange);
    assert(wrapped.gyroX == sample.gyro[0]);
    assert(wrapped.gyroY == -sample.gyro[1]);
    assert(wrapped.gyroZ == -sample.gyro[2]);

    return 0;
}
