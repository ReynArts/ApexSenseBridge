#include "core/TriggerResetGuard.h"
#include "flydigi/Apex5Device.h"
#include "flydigi/Apex5Protocol.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

std::atomic_bool g_stopRequested{false};

#ifdef _WIN32
BOOL WINAPI consoleHandler(DWORD event) {
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        g_stopRequested.store(true, std::memory_order_relaxed);
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

std::string narrowAscii(const std::wstring& value) {
    std::string out;
    out.reserve(value.size());
    for (const wchar_t ch : value) {
        out.push_back(ch >= 32 && ch <= 126 ? static_cast<char>(ch) : '?');
    }
    return out;
}

std::string hex16(std::uint16_t value) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << value;
    return oss.str();
}

void printDevice(const asb::HidDeviceInfo& info, std::size_t index) {
    std::cout << "[" << index << "] "
              << (info.product.empty() ? "Flydigi controller" : narrowAscii(info.product)) << "\n"
              << "    VID:PID      " << hex16(info.vendorId) << ":" << hex16(info.productId) << "\n"
              << "    Usage page   " << hex16(info.usagePage) << "  usage " << hex16(info.usage) << "\n"
              << "    Reports      input=" << info.inputReportLength
              << " output=" << info.outputReportLength << " bytes\n";
}

void printUsage() {
    std::cout
        << "ApexSenseBridge 0.2\n\n"
        << "Commands:\n"
        << "  list                         List APEX 5 vendor HID candidates\n"
        << "  test-rt [index]              Gentle RT FORCEADAPT test (~1.5 s)\n"
        << "  clear [index]                Clear LT + RT effects\n"
        << "  dry-run                      Print the test packet without HID I/O\n\n"
        << "The hardware-writing commands only target Flydigi VID 37D7, controller PID family 2xxx,\n"
        << "and vendor usage page FFA0. If more than one candidate is found, pass its list index.\n";
}

std::optional<std::size_t> parseIndex(int argc, char** argv) {
    if (argc < 3) {
        return std::nullopt;
    }
    try {
        return static_cast<std::size_t>(std::stoul(argv[2]));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<asb::flydigi::Apex5Device> openSelected(int argc, char** argv, std::string& error) {
    auto candidates = asb::flydigi::Apex5Device::findCandidates(error);
    if (!error.empty() && candidates.empty()) {
        return std::nullopt;
    }
    if (candidates.empty()) {
        error = "No APEX 5 vendor HID interface found. Wake the controller and keep the 2.4 GHz dongle connected.";
        return std::nullopt;
    }

    std::size_t index = 0;
    if (const auto requested = parseIndex(argc, argv)) {
        index = *requested;
    } else if (candidates.size() > 1) {
        error = "More than one Flydigi controller vendor interface was found. Run 'list' and pass the wanted index.";
        return std::nullopt;
    }

    if (index >= candidates.size()) {
        error = "Device index is out of range. Run 'list' first.";
        return std::nullopt;
    }

    return asb::flydigi::Apex5Device::open(candidates[index], error);
}

int commandList() {
    std::string error;
    const auto candidates = asb::flydigi::Apex5Device::findCandidates(error);
    if (!error.empty()) {
        std::cerr << "HID enumeration warning: " << error << "\n";
    }
    if (candidates.empty()) {
        std::cout << "No APEX 5 vendor HID interface found.\n";
        return 2;
    }
    std::cout << "Found " << candidates.size() << " candidate(s):\n\n";
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        printDevice(candidates[i], i);
    }
    return 0;
}

int commandDryRun() {
    asb::TriggerEffect effect{};
    effect.side = asb::TriggerSide::Right;
    effect.mode = asb::TriggerMode::Race;
    effect.start = 70;
    effect.p1 = 30;
    effect.matchInput = false;

    const auto report = asb::flydigi::buildForceTrigger(effect);
    for (std::size_t i = 0; i < report.size(); ++i) {
        if (i != 0 && i % 16 == 0) {
            std::cout << '\n';
        }
        std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                  << static_cast<int>(report[i]) << ' ';
    }
    std::cout << std::dec << "\n";
    return 0;
}

int commandClear(int argc, char** argv) {
    std::string error;
    auto device = openSelected(argc, argv, error);
    if (!device) {
        std::cerr << "Error: " << error << "\n";
        return 3;
    }
    if (!device->clearAll(error)) {
        std::cerr << "Error: " << error << "\n";
        return 4;
    }
    std::cout << "LT and RT reset to Normal.\n";
    return 0;
}

int commandTestRt(int argc, char** argv) {
    std::string error;
    auto device = openSelected(argc, argv, error);
    if (!device) {
        std::cerr << "Error: " << error << "\n";
        return 3;
    }

    std::cout << "Using: " << narrowAscii(device->info().product) << " ("
              << hex16(device->info().vendorId) << ':' << hex16(device->info().productId) << ")\n";
    std::cout << "Applying a GENTLE RT resistance for about 1.5 seconds...\n";

    asb::TriggerResetGuard resetOnExit(*device);

    asb::TriggerEffect effect{};
    effect.side = asb::TriggerSide::Right;
    effect.mode = asb::TriggerMode::Race;
    effect.start = 70;
    effect.p1 = 30; // intentionally gentle for the first hardware test
    effect.matchInput = false;

    if (!device->setTrigger(effect, error)) {
        std::cerr << "Write failed: " << error << "\n";
        return 4;
    }

    constexpr auto duration = std::chrono::milliseconds(1500);
    constexpr auto slice = std::chrono::milliseconds(25);
    auto elapsed = std::chrono::milliseconds::zero();
    while (elapsed < duration && !g_stopRequested.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(slice);
        elapsed += slice;
    }

    if (!device->clearAll(error)) {
        std::cerr << "WARNING: automatic reset write failed: " << error << "\n"
                  << "Open Flydigi Space Station and set both triggers to Normal before continuing.\n";
        return 5;
    }
    resetOnExit.dismiss();

    std::cout << "RT reset to Normal.\n"
              << "If you felt a resistance begin part-way through RT, the Windows -> APEX FORCEADAPT path works.\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#endif

    if (argc < 2) {
        printUsage();
        return 0;
    }

    const std::string_view command = argv[1];
    if (command == "list") {
        return commandList();
    }
    if (command == "test-rt") {
        return commandTestRt(argc, argv);
    }
    if (command == "clear") {
        return commandClear(argc, argv);
    }
    if (command == "dry-run") {
        return commandDryRun();
    }

    printUsage();
    return 1;
}
