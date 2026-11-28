// Verifies the APEX 5 rear-button bit mapping decoded from byte 14 of the
// vendor HID input report (observed on hardware: M1=0x04 ... RM=0x80).
#include "flydigi/Apex5RearButtons.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {

using asb::flydigi::apex5::RearButton;

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        ++failures;
    }
}

} // namespace

int main() {
    using namespace asb::flydigi::apex5;

    expect(rearButtonMask(RearButton::M1) == 0x04, "M1 mask must be 0x04");
    expect(rearButtonMask(RearButton::M2) == 0x08, "M2 mask must be 0x08");
    expect(rearButtonMask(RearButton::M3) == 0x10, "M3 mask must be 0x10");
    expect(rearButtonMask(RearButton::M4) == 0x20, "M4 mask must be 0x20");
    expect(rearButtonMask(RearButton::LM) == 0x40, "LM mask must be 0x40");
    expect(rearButtonMask(RearButton::RM) == 0x80, "RM mask must be 0x80");
    expect(kRearButtonByteOffset == 14, "rear buttons live in byte 14");

    // A short report must never be read past its end.
    const std::array<std::uint8_t, 10> shortReport{};
    expect(rearButtonBits(shortReport) == 0, "short report yields no bits");
    expect(!isRearButtonPressed(shortReport, RearButton::M1), "short report has no presses");

    std::array<std::uint8_t, 32> report{};
    expect(rearButtonBits(report) == 0, "idle report yields no bits");

    // Each button sets exactly its own bit and nothing else.
    const RearButton buttons[] = {RearButton::M1, RearButton::M2, RearButton::M3,
                                  RearButton::M4, RearButton::LM, RearButton::RM};
    for (const RearButton button : buttons) {
        report[kRearButtonByteOffset] = rearButtonMask(button);
        for (const RearButton other : buttons) {
            const bool expected = other == button;
            expect(isRearButtonPressed(report, other) == expected,
                   "exactly one rear button must be reported as pressed");
        }
    }

    // Chords keep every bit independent.
    report[kRearButtonByteOffset] = 0x04 | 0x20;
    expect(isRearButtonPressed(report, RearButton::M1), "chord includes M1");
    expect(isRearButtonPressed(report, RearButton::M4), "chord includes M4");
    expect(!isRearButtonPressed(report, RearButton::M2), "chord excludes M2");

    if (failures == 0) {
        std::puts("All Apex5 rear button tests passed.");
        return 0;
    }
    return 1;
}
