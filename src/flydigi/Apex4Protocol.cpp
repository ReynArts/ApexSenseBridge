#include "flydigi/Apex4Protocol.h"

#include <algorithm>
#include <cstdint>

namespace asb::flydigi {
namespace {

std::uint8_t atLeastOne(std::uint8_t value) noexcept {
    return std::max<std::uint8_t>(1, value);
}

Apex4ForceTriggerReport baseForceTrigger(bool apply,
                                         TriggerSide side,
                                         TriggerMode mode) {
    Apex4ForceTriggerReport report{};
    report[0] = kApex4CommandReportId;
    report[1] = kApex4CmdSetForceTriggerDInput;
    // Flydigi's DInput factory puts an effect-family selector before the
    // apply flag. Omitting it still produces a successful HID write, but the
    // Apex 4 firmware ignores the shifted effect payload.
    report[2] = kApex4ForceTriggerEffectFamily;
    report[3] = static_cast<std::uint8_t>(apply ? 1 : 0);
    report[4] = static_cast<std::uint8_t>(side);
    report[5] = static_cast<std::uint8_t>(mode);
    return report;
}

} // namespace

bool isApex4Product(std::uint16_t vendorId,
                    std::uint16_t productId) noexcept {
    return vendorId == kApex4VendorId && productId == kApex4ProductId;
}

Apex4IdentityRequest buildApex4IdentityRequest() {
    Apex4IdentityRequest report{};
    report[0] = kApex4CommandReportId;
    report[1] = kApex4CmdGetInfo;
    return report;
}

Apex4ForceTriggerReport buildApex4ForceTrigger(const TriggerEffect& effect,
                                               bool apply) {
    auto report = baseForceTrigger(apply, effect.side, effect.mode);

    switch (effect.mode) {
    case TriggerMode::Normal:
        break;

    case TriggerMode::Race:
        report[6] = effect.start;
        report[7] = atLeastOne(effect.p1);
        report[8] = static_cast<std::uint8_t>(
            effect.start == 0 && effect.matchInput ? 0 : effect.matchInput ? 1 : 0);
        break;

    case TriggerMode::RecoilRattle:
    case TriggerMode::Vibration:
        report[6] = effect.start;
        report[7] = atLeastOne(effect.p1);
        report[8] = atLeastOne(effect.p2);
        report[9] = atLeastOne(effect.p3);
        report[10] = static_cast<std::uint8_t>(effect.matchInput ? 1 : 0);
        break;

    case TriggerMode::SniperBreak:
        report[6] = effect.start;
        report[7] = atLeastOne(effect.p1);
        report[8] = atLeastOne(effect.p2);
        report[9] = 0;
        report[10] = static_cast<std::uint8_t>(effect.matchInput ? 1 : 0);
        break;

    case TriggerMode::Lock:
        report[6] = effect.start;
        report[7] = 255;
        report[8] = static_cast<std::uint8_t>(effect.matchInput ? 1 : 0);
        break;
    }

    return report;
}

Apex4ForceTriggerReport buildApex4ForceTriggerRaw(
    const ForceTriggerCommand& command,
    bool apply) {
    auto report = baseForceTrigger(apply, command.side, command.mode);
    std::copy(command.params.begin(), command.params.end(), report.begin() + 6);

    // Flydigi ForceTriggerConfigCommon applies this rewrite on live effects.
    if (command.mode == TriggerMode::Race && report[6] == 0 && report[8] == 1) {
        report[8] = 0;
    }
    return report;
}

Apex4ForceTriggerReport buildApex4Normal(TriggerSide side) {
    TriggerEffect effect{};
    effect.side = side;
    effect.mode = TriggerMode::Normal;
    return buildApex4ForceTrigger(effect, true);
}

Apex4RumbleReport buildApex4Rumble(std::uint8_t lowFrequencyMotor,
                                   std::uint8_t highFrequencyMotor) {
    return {kApex4CommandReportId, kApex4CmdRumble,
            lowFrequencyMotor, highFrequencyMotor};
}

} // namespace asb::flydigi
