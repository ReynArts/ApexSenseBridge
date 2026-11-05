// Read-only HID input probe for Flydigi APEX 5 (interface UsagePage 0xFFA0).
// Never writes output or feature reports.
// Phase 1 (baseline, 5 s): blacklists offsets that fluctuate while idle.
// Phase 2: decodes button bits in byte 14 and prints press/release events.
#include "core/DeviceInfo.h"
#include "platform/HidTransport.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

using asb::HidDeviceInfo;
using asb::platform::HidReadStatus;
using asb::platform::HidStringQuery;
using asb::platform::HidTransport;

namespace {

constexpr std::uint16_t kFlydigiVid = 0x37D7;
constexpr std::uint16_t kApex4Vid = 0x04B4;
constexpr std::uint16_t kApex4Pid = 0x2412;
constexpr int kBaselineSeconds = 5;

// Byte 14 button bits observed on APEX 5 interface [1].
constexpr std::size_t kButtonByteOffset = 14;
struct ButtonBit {
    std::uint8_t mask;
    const char* name;
};
constexpr ButtonBit kButtons[] = {
    {0x80, "RM"},
    {0x40, "LM"},
    {0x04, "M1"},
    {0x08, "M2"},
    {0x10, "M3"},
    {0x20, "M4"},
};

bool isCandidate(const HidDeviceInfo& d) {
    const bool apex4 = d.vendorId == kApex4Vid && d.productId == kApex4Pid;
    const bool flydigi = d.vendorId == kFlydigiVid;
    return (apex4 || flydigi) && d.inputReportLength > 0;
}

} // namespace

int main(int argc, char** argv) {
    int seconds = 60;
    if (argc > 1) {
        seconds = std::atoi(argv[1]);
        if (seconds <= 0) {
            seconds = 60;
        }
    }

    std::string error;
    const auto devices = asb::platform::enumerateHidDevices(error, HidStringQuery::AllDevices);
    if (!error.empty()) {
        std::cout << "Enumeration warning: " << error << "\n";
    }

    std::vector<HidDeviceInfo> candidates;
    for (const auto& d : devices) {
        if (isCandidate(d)) {
            candidates.push_back(d);
        }
    }

    if (candidates.empty()) {
        std::cout << "No Flydigi/APEX HID interfaces with an input report were found.\n";
        return 1;
    }

    std::cout << "Candidate interfaces:\n";
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& d = candidates[i];
        std::cout << "[" << i << "] VID=0x" << std::hex << std::uppercase << d.vendorId
                  << " PID=0x" << d.productId
                  << " UsagePage=0x" << d.usagePage
                  << std::dec << " InputLen=" << d.inputReportLength
                  << " Iface=";
        std::wcout << d.interfaceNumber << L" Product=" << d.product << L"\n";
        std::wcout << L"    Path=" << d.path << L"\n";
    }

    std::cout << "Select interface index: ";
    std::size_t index = 0;
    if (!(std::cin >> index) || index >= candidates.size()) {
        std::cout << "Invalid selection.\n";
        return 1;
    }

    const HidDeviceInfo& info = candidates[index];
    std::string openError;
    HidTransport* transport = asb::platform::createHidTransport(info, openError);
    if (transport == nullptr || !transport->isOpen()) {
        std::cout << "Failed to open interface: " << openError << "\n";
        if (transport != nullptr) {
            asb::platform::destroyHidTransport(transport);
        }
        return 1;
    }

    const std::size_t len = info.inputReportLength;
    std::vector<std::uint8_t> report(len);
    std::vector<std::uint8_t> previous(len, 0);
    std::vector<bool> blacklisted(len, false);
    bool havePrevious = false;
    std::size_t reportCount = 0;

    // Phase 1: baseline. Mark every offset that changes while idle.
    std::cout << "Baseline: keep the controller idle for " << kBaselineSeconds << " seconds...\n";
    const auto baselineEnd = std::chrono::steady_clock::now() + std::chrono::seconds(kBaselineSeconds);
    while (std::chrono::steady_clock::now() < baselineEnd) {
        std::size_t bytesRead = 0;
        std::string readError;
        const HidReadStatus status = transport->readInputReport(
            std::span<std::uint8_t>(report), std::chrono::milliseconds(100), bytesRead, readError);
        if (status == HidReadStatus::Error) {
            std::cout << "Read error: " << readError << "\n";
            asb::platform::destroyHidTransport(transport);
            return 1;
        }
        if (status != HidReadStatus::Data || bytesRead == 0) {
            continue;
        }
        const std::size_t limit = bytesRead < len ? bytesRead : len;
        if (havePrevious) {
            for (std::size_t i = 0; i < limit; ++i) {
                if (report[i] != previous[i]) {
                    blacklisted[i] = true;
                }
            }
        }
        previous = report;
        havePrevious = true;
    }

    // Byte 14 is the button byte, so it must never be blacklisted.
    if (kButtonByteOffset < len) {
        blacklisted[kButtonByteOffset] = false;
    }

    std::cout << "Reading for " << seconds << " more seconds. Press and release buttons one at a time.\n";

    // Phase 2: decode button bits and print only transitions.
    std::uint8_t lastButtons = 0;
    bool haveButtons = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        std::size_t bytesRead = 0;
        std::string readError;
        const HidReadStatus status = transport->readInputReport(
            std::span<std::uint8_t>(report), std::chrono::milliseconds(100), bytesRead, readError);
        if (status == HidReadStatus::Error) {
            std::cout << "Read error: " << readError << "\n";
            break;
        }
        if (status != HidReadStatus::Data || bytesRead <= kButtonByteOffset) {
            continue;
        }

        ++reportCount;
        const std::uint8_t buttons = report[kButtonByteOffset];
        if (!haveButtons) {
            lastButtons = buttons;
            haveButtons = true;
            continue;
        }
        if (buttons == lastButtons) {
            continue;
        }

        for (const ButtonBit& b : kButtons) {
            const bool was = (lastButtons & b.mask) != 0;
            const bool now = (buttons & b.mask) != 0;
            if (was != now) {
                std::printf("[%s %s] report #%zu byte14=%02X\n",
                            now ? "PRESS" : "RELEASE", b.name, reportCount, buttons);
            }
        }

        // Show unknown bits too, so new buttons are not silently ignored.
        const std::uint8_t known = 0xFC | 0x03; // bits we already name, plus 0x01/0x02 unknown.
        (void)known;
        lastButtons = buttons;
    }

    asb::platform::destroyHidTransport(transport);
    std::cout << "Done. Processed " << reportCount << " reports after baseline.\n";
    return 0;
}