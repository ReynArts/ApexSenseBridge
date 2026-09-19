#ifdef NDEBUG
#undef NDEBUG
#endif

#include "dualsense/DualSenseDescriptor.h"
#include "platform/linux/LinuxHidReportDescriptor.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

using asb::platform::HidReportDescriptorInfo;
using asb::platform::parseHidReportDescriptor;

void testRealDualSenseDescriptor() {
    HidReportDescriptorInfo info{};
    assert(parseHidReportDescriptor(asb::dualsense::usbReportDescriptor(), info));

    // Generic Desktop / Game Pad.
    assert(info.usagePage == 0x01);
    assert(info.usage == 0x05);
    // Report 0x01 declares 63 data bytes, plus the report-ID byte.
    assert(info.inputReportLength == 64);
    // Report 0x02 declares 47 data bytes, plus the report-ID byte.
    assert(info.outputReportLength == 48);
    // The widest feature report declares 63 data bytes, plus the report ID.
    assert(info.featureReportLength == 64);
}

// An APEX-shaped vendor collection: usage page 0xFFA0, numbered 32-byte
// reports in both directions and a 46-byte feature report, which is exactly
// the shape the vendor-interface candidate filter keys on.
void testVendorCollectionWithNumberedReports() {
    const std::vector<std::uint8_t> descriptor{
        0x06, 0xA0, 0xFF,       // Usage Page (Vendor 0xFFA0)
        0x09, 0x01,             // Usage (0x01)
        0xA1, 0x01,             // Collection (Application)
        0x85, 0x03,             //   Report ID (3)
        0x09, 0x02,             //   Usage (0x02)
        0x75, 0x08,             //   Report Size (8)
        0x95, 0x1F,             //   Report Count (31)
        0x91, 0x02,             //   Output
        0x85, 0x04,             //   Report ID (4)
        0x09, 0x03,             //   Usage (0x03)
        0x75, 0x08,             //   Report Size (8)
        0x95, 0x1F,             //   Report Count (31)
        0x81, 0x02,             //   Input
        0x85, 0x05,             //   Report ID (5)
        0x09, 0x04,             //   Usage (0x04)
        0x75, 0x08,             //   Report Size (8)
        0x95, 0x2D,             //   Report Count (45)
        0xB1, 0x02,             //   Feature
        0xC0,                   // End Collection
    };

    HidReportDescriptorInfo info{};
    assert(parseHidReportDescriptor(descriptor, info));
    assert(info.usagePage == 0xFFA0);
    assert(info.usage == 0x01);
    // 31 data bytes + report ID on each side; 45 + 1 for the feature report.
    assert(info.inputReportLength == 32);
    assert(info.outputReportLength == 32);
    assert(info.featureReportLength == 46);
}

// Without a Report ID item nothing is numbered, so no byte is added.
void testUnnumberedReportsDoNotGainAByte() {
    const std::vector<std::uint8_t> descriptor{
        0x05, 0x01,             // Usage Page (Generic Desktop)
        0x09, 0x05,             // Usage (Game Pad)
        0xA1, 0x01,             // Collection (Application)
        0x75, 0x08,             //   Report Size (8)
        0x95, 0x08,             //   Report Count (8)
        0x81, 0x02,             //   Input
        0xC0,                   // End Collection
    };

    HidReportDescriptorInfo info{};
    assert(parseHidReportDescriptor(descriptor, info));
    assert(info.inputReportLength == 8);
    assert(info.outputReportLength == 0);
    assert(info.featureReportLength == 0);
}

// Several main items feeding one report accumulate, and a report shorter than
// a whole byte still rounds up.
void testBitAccumulationAndRounding() {
    const std::vector<std::uint8_t> descriptor{
        0x05, 0x01,
        0x09, 0x05,
        0xA1, 0x01,
        0x85, 0x01,             //   Report ID (1)
        0x75, 0x01,             //   Report Size (1)
        0x95, 0x03,             //   Report Count (3)   -> 3 bits
        0x81, 0x02,             //   Input
        0x75, 0x01,             //   Report Size (1)
        0x95, 0x06,             //   Report Count (6)   -> 6 bits, 9 total
        0x81, 0x02,             //   Input
        0xC0,
    };

    HidReportDescriptorInfo info{};
    assert(parseHidReportDescriptor(descriptor, info));
    // 9 bits rounds to 2 bytes, plus the report-ID byte.
    assert(info.inputReportLength == 3);
}

// Push/Pop must restore the global item state, including the report ID.
void testGlobalPushPop() {
    const std::vector<std::uint8_t> descriptor{
        0x05, 0x01,
        0x09, 0x05,
        0xA1, 0x01,
        0x85, 0x01,             //   Report ID (1)
        0x75, 0x08,             //   Report Size (8)
        0x95, 0x04,             //   Report Count (4)
        0xA4,                   //   Push
        0x85, 0x02,             //     Report ID (2)
        0x95, 0x10,             //     Report Count (16)
        0x81, 0x02,             //     Input  -> report 2 gets 16 bytes
        0xB4,                   //   Pop
        0x81, 0x02,             //   Input    -> report 1 gets 4 bytes
        0xC0,
    };

    HidReportDescriptorInfo info{};
    assert(parseHidReportDescriptor(descriptor, info));
    // The widest report wins: 16 data bytes plus the report ID.
    assert(info.inputReportLength == 17);
}

// Truncated items are rejected rather than read past the end.
void testMalformedDescriptorsAreRejected() {
    HidReportDescriptorInfo info{};
    // Item claims two data bytes but supplies one.
    const std::vector<std::uint8_t> truncated{0x06, 0xA0};
    assert(!parseHidReportDescriptor(truncated, info));

    // Well-formed but carries nothing the bridge can use.
    const std::vector<std::uint8_t> empty{};
    assert(!parseHidReportDescriptor(empty, info));

    // Pop without a matching Push is malformed.
    const std::vector<std::uint8_t> unbalanced{0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0xB4, 0xC0};
    assert(!parseHidReportDescriptor(unbalanced, info));
}

} // namespace

int main() {
    testRealDualSenseDescriptor();
    testVendorCollectionWithNumberedReports();
    testUnnumberedReportsDoNotGainAByte();
    testBitAccumulationAndRounding();
    testGlobalPushPop();
    testMalformedDescriptorsAreRejected();
    return 0;
}
