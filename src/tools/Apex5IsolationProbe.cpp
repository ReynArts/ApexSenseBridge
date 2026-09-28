#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "dualsense/DualSenseInput.h"
#include "flydigi/Apex5Device.h"
#include "flydigi/Apex5Protocol.h"
#include "platform/PhysicalControllerIsolation.h"
#include "platform/PhysicalInputSource.h"
#include "platform/XInputGamepad.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using asb::dualsense::DualSenseInputState;
using asb::flydigi::Apex5Device;
using asb::flydigi::InputTransportStatus;
using asb::platform::PhysicalInputSourceStats;

constexpr unsigned kSampleSeconds = 8;
std::atomic_bool gStopRequested{false};

BOOL WINAPI consoleHandler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT ||
        event == CTRL_CLOSE_EVENT || event == CTRL_LOGOFF_EVENT ||
        event == CTRL_SHUTDOWN_EVENT) {
        gStopRequested.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}

std::string jsonEscape(std::string_view value) {
    std::ostringstream escaped;
    for (const unsigned char character : value) {
        switch (character) {
        case '"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\b': escaped << "\\b"; break;
        case '\f': escaped << "\\f"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:
            if (character < 0x20) {
                escaped << "\\u00" << std::hex << std::setw(2)
                        << std::setfill('0')
                        << static_cast<unsigned int>(character) << std::dec;
            } else {
                escaped << static_cast<char>(character);
            }
        }
    }
    return escaped.str();
}

std::string narrowAscii(std::wstring_view value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        result.push_back(character >= 32 && character <= 126
                             ? static_cast<char>(character)
                             : '?');
    }
    return result;
}

std::string utcTimestamp() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    std::ostringstream value;
    value << std::setfill('0') << std::setw(4) << time.wYear << '-'
          << std::setw(2) << time.wMonth << '-' << std::setw(2) << time.wDay
          << 'T' << std::setw(2) << time.wHour << ':' << std::setw(2)
          << time.wMinute << ':' << std::setw(2) << time.wSecond << 'Z';
    return value.str();
}

std::filesystem::path executableDirectory() {
    std::vector<wchar_t> buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(
        std::wstring(buffer.data(), length)).parent_path();
}

struct InputSample {
    bool sourceOpened = false;
    bool receivedState = false;
    bool xinputAccessible = false;
    int xinputIndex = -1;
    std::string backend;
    std::string openWarning;
    std::string streamError;
    std::string xinputError;
    std::vector<unsigned int> connectedXInput;
    std::uint64_t stateChanges = 0;
    std::uint32_t seenButtons = 0;
    std::uint8_t seenDpad = 0;
    std::uint8_t minimumLx = 255;
    std::uint8_t maximumLx = 0;
    std::uint8_t minimumLy = 255;
    std::uint8_t maximumLy = 0;
    std::uint8_t minimumRx = 255;
    std::uint8_t maximumRx = 0;
    std::uint8_t minimumRy = 255;
    std::uint8_t maximumRy = 0;
    std::uint8_t maximumL2 = 0;
    std::uint8_t maximumR2 = 0;
    PhysicalInputSourceStats stats{};
};

void snapshotXInput(const asb::HidDeviceInfo& info, InputSample& sample) {
    sample.connectedXInput = asb::platform::connectedXInputGamepads();
    std::string error;
    auto xinput = asb::platform::openXInputGamepadForDevice(
        info.vendorId, info.productId, std::nullopt, error);
    if (!xinput) {
        sample.xinputError = std::move(error);
        return;
    }
    DualSenseInputState state{};
    if (!xinput->poll(state, error)) {
        sample.xinputError = std::move(error);
        return;
    }
    sample.xinputAccessible = true;
    sample.xinputIndex = static_cast<int>(xinput->index());
}

InputSample collectInputSample(const asb::HidDeviceInfo& info,
                               unsigned seconds) {
    InputSample sample{};
    snapshotXInput(info, sample);

    std::string error;
    auto source = asb::platform::openPhysicalInputSource(
        info, std::nullopt, error);
    if (!source) {
        sample.streamError = std::move(error);
        return sample;
    }
    sample.sourceOpened = true;
    sample.backend = std::string(source->backendName());
    sample.openWarning = std::move(error);

    DualSenseInputState previous{};
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline &&
           !gStopRequested.load(std::memory_order_relaxed)) {
        DualSenseInputState state{};
        error.clear();
        const auto status = source->waitForState(
            state, std::chrono::milliseconds(200), error);
        if (status == asb::platform::PhysicalInputStatus::State) {
            if (!sample.receivedState || state != previous) ++sample.stateChanges;
            sample.receivedState = true;
            previous = state;
            sample.seenButtons |= state.buttons;
            sample.seenDpad = static_cast<std::uint8_t>(
                sample.seenDpad | state.dpad);
            sample.minimumLx = (std::min)(sample.minimumLx, state.lx);
            sample.maximumLx = (std::max)(sample.maximumLx, state.lx);
            sample.minimumLy = (std::min)(sample.minimumLy, state.ly);
            sample.maximumLy = (std::max)(sample.maximumLy, state.ly);
            sample.minimumRx = (std::min)(sample.minimumRx, state.rx);
            sample.maximumRx = (std::max)(sample.maximumRx, state.rx);
            sample.minimumRy = (std::min)(sample.minimumRy, state.ry);
            sample.maximumRy = (std::max)(sample.maximumRy, state.ry);
            sample.maximumL2 = (std::max)(sample.maximumL2, state.l2);
            sample.maximumR2 = (std::max)(sample.maximumR2, state.r2);
        } else if (status == asb::platform::PhysicalInputStatus::Disconnected ||
                   status == asb::platform::PhysicalInputStatus::Error) {
            sample.streamError = std::move(error);
            break;
        }
    }
    sample.stats = source->stats();
    return sample;
}

std::optional<Apex5Device> openOnlyApex5(asb::HidDeviceInfo& selectedInfo,
                                         std::string& error) {
    std::string enumerationError;
    const auto candidates = Apex5Device::findCandidates(enumerationError);
    std::optional<Apex5Device> selected;
    std::string lastError = std::move(enumerationError);
    for (const auto& candidate : candidates) {
        std::string candidateError;
        auto device = Apex5Device::open(candidate, candidateError);
        if (!device || !device->verifyIdentity(candidateError) ||
            !device->identity() || !device->identity()->isApex5()) {
            if (!candidateError.empty()) lastError = std::move(candidateError);
            continue;
        }
        if (selected) {
            error = "More than one verified Apex 5 is connected. Disconnect all "
                    "but the controller under test.";
            return std::nullopt;
        }
        selectedInfo = candidate;
        selected.emplace(std::move(*device));
    }
    if (!selected) {
        error = lastError.empty()
            ? "No verified Apex 5 vendor interface was found."
            : std::move(lastError);
    }
    return selected;
}

void appendTransportJson(std::ostringstream& json,
                         const std::optional<InputTransportStatus>& status,
                         unsigned indentation) {
    const std::string spaces(indentation, ' ');
    if (!status) {
        json << "null";
        return;
    }
    json << "{\n"
         << spaces << "  \"controller_data\": "
         << (status->controllerData ? "true" : "false") << ",\n"
         << spaces << "  \"raw_data\": "
         << (status->rawData ? "true" : "false") << ",\n"
         << spaces << "  \"keyboard_data\": "
         << (status->keyboardData ? "true" : "false") << ",\n"
         << spaces << "  \"mouse_data\": "
         << (status->mouseData ? "true" : "false") << ",\n"
         << spaces << "  \"third_party_control\": "
         << (status->thirdPartyControl ? "true" : "false") << '\n'
         << spaces << '}';
}

void appendUnsignedArray(std::ostringstream& json,
                         const std::vector<unsigned int>& values) {
    json << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) json << ", ";
        json << values[index];
    }
    json << ']';
}

void appendSampleJson(std::ostringstream& json, const InputSample& sample,
                      unsigned indentation) {
    const std::string spaces(indentation, ' ');
    json << "{\n"
         << spaces << "  \"source_opened\": "
         << (sample.sourceOpened ? "true" : "false") << ",\n"
         << spaces << "  \"backend\": \"" << jsonEscape(sample.backend)
         << "\",\n"
         << spaces << "  \"received_state\": "
         << (sample.receivedState ? "true" : "false") << ",\n"
         << spaces << "  \"state_changes\": " << sample.stateChanges << ",\n"
         << spaces << "  \"reports\": " << sample.stats.reports << ",\n"
         << spaces << "  \"timeouts\": " << sample.stats.timeouts << ",\n"
         << spaces << "  \"parse_failures\": "
         << sample.stats.parseFailures << ",\n"
         << spaces << "  \"mapped_reports\": "
         << sample.stats.mappedReports << ",\n"
         << spaces << "  \"vendor_reports\": "
         << sample.stats.vendorReports << ",\n"
         << spaces << "  \"vendor_states\": "
         << sample.stats.vendorStates << ",\n"
         << spaces << "  \"vendor_parse_failures\": "
         << sample.stats.vendorParseFailures << ",\n"
         << spaces << "  \"vendor_read_failures\": "
         << sample.stats.vendorReadFailures << ",\n"
         << spaces << "  \"xinput_accessible\": "
         << (sample.xinputAccessible ? "true" : "false") << ",\n"
         << spaces << "  \"xinput_index\": " << sample.xinputIndex << ",\n"
         << spaces << "  \"connected_xinput_slots\": ";
    appendUnsignedArray(json, sample.connectedXInput);
    json << ",\n"
         << spaces << "  \"seen_buttons\": " << sample.seenButtons << ",\n"
         << spaces << "  \"seen_dpad\": "
         << static_cast<unsigned int>(sample.seenDpad) << ",\n"
         << spaces << "  \"stick_ranges\": {\n"
         << spaces << "    \"lx\": ["
         << static_cast<unsigned int>(sample.minimumLx) << ", "
         << static_cast<unsigned int>(sample.maximumLx) << "],\n"
         << spaces << "    \"ly\": ["
         << static_cast<unsigned int>(sample.minimumLy) << ", "
         << static_cast<unsigned int>(sample.maximumLy) << "],\n"
         << spaces << "    \"rx\": ["
         << static_cast<unsigned int>(sample.minimumRx) << ", "
         << static_cast<unsigned int>(sample.maximumRx) << "],\n"
         << spaces << "    \"ry\": ["
         << static_cast<unsigned int>(sample.minimumRy) << ", "
         << static_cast<unsigned int>(sample.maximumRy) << "]\n"
         << spaces << "  },\n"
         << spaces << "  \"maximum_l2\": "
         << static_cast<unsigned int>(sample.maximumL2) << ",\n"
         << spaces << "  \"maximum_r2\": "
         << static_cast<unsigned int>(sample.maximumR2) << ",\n"
         << spaces << "  \"open_warning\": \""
         << jsonEscape(sample.openWarning) << "\",\n"
         << spaces << "  \"stream_error\": \""
         << jsonEscape(sample.streamError) << "\",\n"
         << spaces << "  \"xinput_error\": \""
         << jsonEscape(sample.xinputError) << "\"\n"
         << spaces << '}';
}

struct ProbeReport {
    std::string generatedUtc = utcTimestamp();
    std::string product;
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    int profileSlot = -1;
    std::optional<InputTransportStatus> originalTransport;
    std::optional<InputTransportStatus> strictTransport;
    std::optional<InputTransportStatus> restoredTransport;
    InputSample baseline;
    InputSample strict;
    InputSample recovered;
    bool isolationActivated = false;
    bool recoveryArmed = false;
    bool strictRoutingApplied = false;
    bool restoreAttempted = false;
    bool restoreSucceeded = false;
    bool xinputSuppressed = false;
    bool mappedHidSurvived = false;
    bool vendorStreamSurvived = false;
    bool transportRestored = false;
    bool minimalFixCandidate = false;
    bool interrupted = false;
    std::string fatalError;
    std::string restoreError;
};

std::string serializeReport(const ProbeReport& report) {
    std::ostringstream json;
    json << "{\n"
         << "  \"probe\": \"Apex5IsolationProbe\",\n"
         << "  \"probe_version\": \"1.0.0\",\n"
         << "  \"generated_utc\": \"" << report.generatedUtc << "\",\n"
         << "  \"device\": {\n"
         << "    \"product\": \"" << jsonEscape(report.product) << "\",\n"
         << "    \"vendor_id\": " << report.vendorId << ",\n"
         << "    \"product_id\": " << report.productId << ",\n"
         << "    \"profile_slot\": " << report.profileSlot << "\n"
         << "  },\n"
         << "  \"original_transport\": ";
    appendTransportJson(json, report.originalTransport, 2);
    json << ",\n  \"strict_transport\": ";
    appendTransportJson(json, report.strictTransport, 2);
    json << ",\n  \"restored_transport\": ";
    appendTransportJson(json, report.restoredTransport, 2);
    json << ",\n  \"baseline\": ";
    appendSampleJson(json, report.baseline, 2);
    json << ",\n  \"strict\": ";
    appendSampleJson(json, report.strict, 2);
    json << ",\n  \"recovered\": ";
    appendSampleJson(json, report.recovered, 2);
    json << ",\n"
         << "  \"safety\": {\n"
         << "    \"isolation_activated\": "
         << (report.isolationActivated ? "true" : "false") << ",\n"
         << "    \"recovery_armed_before_write\": "
         << (report.recoveryArmed ? "true" : "false") << ",\n"
         << "    \"strict_routing_applied\": "
         << (report.strictRoutingApplied ? "true" : "false") << ",\n"
         << "    \"restore_attempted\": "
         << (report.restoreAttempted ? "true" : "false") << ",\n"
         << "    \"restore_succeeded\": "
         << (report.restoreSucceeded ? "true" : "false") << ",\n"
         << "    \"interrupted\": "
         << (report.interrupted ? "true" : "false") << "\n"
         << "  },\n"
         << "  \"verdict\": {\n"
         << "    \"xinput_suppressed\": "
         << (report.xinputSuppressed ? "true" : "false") << ",\n"
         << "    \"mapped_hid_survived\": "
         << (report.mappedHidSurvived ? "true" : "false") << ",\n"
         << "    \"vendor_stream_survived\": "
         << (report.vendorStreamSurvived ? "true" : "false") << ",\n"
         << "    \"transport_restored\": "
         << (report.transportRestored ? "true" : "false") << ",\n"
         << "    \"minimal_fix_candidate\": "
         << (report.minimalFixCandidate ? "true" : "false") << "\n"
         << "  },\n"
         << "  \"fatal_error\": \"" << jsonEscape(report.fatalError)
         << "\",\n"
         << "  \"restore_error\": \"" << jsonEscape(report.restoreError)
         << "\"\n"
         << "}\n";
    return json.str();
}

std::filesystem::path writeReport(const ProbeReport& report) {
    const auto fileName = std::filesystem::path("apex5-isolation-probe.json");
    std::vector<std::filesystem::path> destinations{
        executableDirectory() / fileName,
        std::filesystem::current_path() / fileName,
    };
    wchar_t temporary[MAX_PATH + 1]{};
    const auto temporaryLength = GetTempPathW(MAX_PATH, temporary);
    if (temporaryLength > 0 && temporaryLength <= MAX_PATH) {
        destinations.emplace_back(std::filesystem::path(temporary) / fileName);
    }
    for (const auto& destination : destinations) {
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        if (!output) continue;
        output << serializeReport(report);
        if (output) return destination;
    }
    return {};
}

bool waitForEnter(std::string_view prompt) {
    std::cout << prompt << std::flush;
    std::string ignored;
    std::getline(std::cin, ignored);
    return !gStopRequested.load(std::memory_order_relaxed);
}

bool sameTransport(const InputTransportStatus& left,
                   const InputTransportStatus& right) {
    return left.controllerData == right.controllerData &&
           left.rawData == right.rawData &&
           left.keyboardData == right.keyboardData &&
           left.mouseData == right.mouseData &&
           left.thirdPartyControl == right.thirdPartyControl;
}

int recoveryCommand() {
    bool recovered = false;
    std::string error;
    if (!asb::platform::TemporaryPhysicalControllerIsolation::recoverPending(
            recovered, error)) {
        std::cerr << "Recovery failed: " << error << '\n';
        return 2;
    }
    std::cout << (recovered ? "Recovery completed.\n"
                            : "No pending recovery was found.\n");
    return 0;
}

int watchdogCommand(int argc, char** argv) {
    if (argc != 3 && argc != 4) return 1;
    try {
        const auto processId = std::stoul(argv[2]);
        if (processId == 0 || processId > 0xFFFFFFFFUL) return 1;
        const std::string_view token = argc == 4 ? argv[3] : "";
        std::string error;
        const auto status =
            asb::platform::TemporaryPhysicalControllerIsolation::watchAndRecover(
                static_cast<std::uint32_t>(processId), token, error);
        return status == 0 ? 0 : 2;
    } catch (...) {
        return 1;
    }
}

void printHelp() {
    std::cout
        << "Apex 5 XInput isolation probe\n\n"
        << "Before running: close ApexSenseBridge, Flydigi Space Station, games, "
           "and Steam; in HidHide, "
           "uncheck 'Enable device hiding', then close the HidHide client. "
           "Disconnect every other gamepad.\n"
        << "The probe preserves the existing HidHide lists and restores its "
           "original enabled/disabled state.\n"
        << "The probe temporarily requests controllerData=false/rawData=true, "
           "then restores the exact original routing.\n"
        << "Move both sticks, press both triggers, the D-pad, and all face/shoulder "
           "buttons during each prompted phase.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2) {
        const std::string_view command = argv[1];
        if (command == "hidhide-watchdog") return watchdogCommand(argc, argv);
        if (command == "restore-controller-visibility") return recoveryCommand();
        if (command == "--help" || command == "-h" || command == "help") {
            printHelp();
            return 0;
        }
        std::cerr << "Unknown option. Use --help.\n";
        return 1;
    }

    SetConsoleCtrlHandler(consoleHandler, TRUE);
    ProbeReport report{};
    asb::platform::TemporaryPhysicalControllerIsolation isolation;
    int exitCode = 1;

    const auto finish = [&report, &exitCode]() {
        report.interrupted = gStopRequested.load(std::memory_order_relaxed);
        const auto path = writeReport(report);
        if (path.empty()) {
            std::cerr << "\nThe JSON report could not be written.\n";
            exitCode = exitCode == 0 ? 8 : exitCode;
        } else {
            std::wcout << L"\nReport written to:\n" << path.wstring() << L"\n";
        }
        std::cout << "Press ENTER to close." << std::flush;
        std::string ignored;
        std::getline(std::cin, ignored);
        return exitCode;
    };

    printHelp();
    std::cout
        << "\nIMPORTANT: This diagnostic changes only the temporary input-routing "
           "bits and HidHide session state. A watchdog is armed before the write.\n\n";
    if (!waitForEnter("Press ENTER to detect the controller, or Ctrl+C to cancel...")) {
        report.fatalError = "Canceled before controller detection.";
        return finish();
    }

    asb::HidDeviceInfo selectedInfo{};
    std::string error;
    auto device = openOnlyApex5(selectedInfo, error);
    if (!device) {
        report.fatalError = "APEX 5 detection failed: " + error;
        std::cerr << report.fatalError << '\n';
        return finish();
    }
    report.product = selectedInfo.product.empty()
        ? "Flydigi Apex 5"
        : narrowAscii(selectedInfo.product);
    report.vendorId = selectedInfo.vendorId;
    report.productId = selectedInfo.productId;

    InputTransportStatus original{};
    if (!device->readInputTransportStatus(original, error)) {
        report.fatalError = "Could not read the original input routing: " + error;
        std::cerr << report.fatalError << '\n';
        return finish();
    }
    report.originalTransport = original;
    asb::flydigi::ProfileStatus profile{};
    if (device->readProfileStatus(profile, error)) {
        report.profileSlot = static_cast<int>(profile.slot + 1);
    }

    if (!waitForEnter(
            "\nBASELINE (8 seconds): press ENTER, then move every control...")) {
        report.fatalError = "Canceled before the baseline sample.";
        return finish();
    }
    report.baseline = collectInputSample(selectedInfo, kSampleSeconds);

    if (gStopRequested.load(std::memory_order_relaxed)) {
        report.fatalError = "Canceled after the baseline sample.";
        return finish();
    }

    error.clear();
    if (!isolation.activate(selectedInfo, "", std::nullopt, error)) {
        report.fatalError = "Could not arm the HidHide recovery transaction: " + error;
        std::cerr << report.fatalError << '\n';
        return finish();
    }
    report.isolationActivated = true;
    if (!isolation.armApexInputTransportRestore(
            original.controllerData, original.rawData, error)) {
        report.fatalError = "Could not persist the original input routing: " + error;
        std::cerr << report.fatalError << '\n';
        report.restoreAttempted = true;
        report.restoreSucceeded = isolation.restore(report.restoreError);
        return finish();
    }
    report.recoveryArmed = true;

    if (!device->setInputTransport(false, true, error)) {
        report.fatalError = "The Apex 5 rejected strict input routing: " + error;
        std::cerr << report.fatalError << '\n';
        device.reset();
        report.restoreAttempted = true;
        report.restoreSucceeded = isolation.restore(report.restoreError);
        return finish();
    }
    report.strictRoutingApplied = true;
    InputTransportStatus strict{};
    if (device->readInputTransportStatus(strict, error)) {
        report.strictTransport = strict;
    } else {
        report.fatalError = "Could not verify strict input routing: " + error;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    if (report.fatalError.empty() && waitForEnter(
            "\nSTRICT MODE (8 seconds): press ENTER, then repeat every control...")) {
        report.strict = collectInputSample(selectedInfo, kSampleSeconds);
    } else if (report.fatalError.empty()) {
        report.fatalError = "Canceled before the strict-mode sample.";
    }

    device.reset();
    report.restoreAttempted = true;
    report.restoreSucceeded = isolation.restore(report.restoreError);
    if (!report.restoreSucceeded && report.fatalError.empty()) {
        report.fatalError = "Automatic restoration failed.";
    }

    if (report.restoreSucceeded) {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        auto restoredDevice = openOnlyApex5(selectedInfo, error);
        if (restoredDevice) {
            InputTransportStatus restored{};
            if (restoredDevice->readInputTransportStatus(restored, error)) {
                report.restoredTransport = restored;
                report.transportRestored = sameTransport(original, restored);
            } else if (report.fatalError.empty()) {
                report.fatalError = "Could not verify restored input routing: " + error;
            }
            restoredDevice.reset();
            report.recovered = collectInputSample(selectedInfo, 3);
        } else if (report.fatalError.empty()) {
            report.fatalError = "Could not reopen the restored Apex 5: " + error;
        }
    }

    report.xinputSuppressed = report.baseline.xinputAccessible &&
                              !report.strict.xinputAccessible;
    report.mappedHidSurvived = report.strict.stats.mappedReports > 0;
    report.vendorStreamSurvived = report.strict.stats.vendorStates > 0;
    report.minimalFixCandidate = report.xinputSuppressed &&
                                 report.mappedHidSurvived &&
                                 report.vendorStreamSurvived &&
                                 report.transportRestored &&
                                 report.restoreSucceeded;

    if (report.restoreSucceeded && report.transportRestored &&
        report.fatalError.empty()) {
        exitCode = 0;
    } else {
        exitCode = 7;
    }

    std::cout << "\nResult: "
              << (report.minimalFixCandidate
                      ? "the minimal routing fix is a candidate for integration."
                      : "more analysis is required; no permanent change was made.")
              << '\n';
    return finish();
}
