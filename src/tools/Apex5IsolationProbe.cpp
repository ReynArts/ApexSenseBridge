#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <setupapi.h>

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
constexpr DWORD kExternalProbeTimeoutMs = 10000;
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

std::filesystem::path executablePath() {
    std::vector<wchar_t> buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

struct ExternalXInputSnapshot {
    bool launched = false;
    std::uint32_t connectedMask = 0;
    std::string error;
};

class ExternalProbeCopy {
public:
    bool create(std::string& error) {
        const auto source = executablePath();
        if (source.empty()) {
            error = "Could not resolve the probe executable path.";
            return false;
        }
        wchar_t temporary[MAX_PATH + 1]{};
        const auto length = GetTempPathW(MAX_PATH, temporary);
        if (length == 0 || length > MAX_PATH) {
            error = "Could not resolve the Windows temporary directory.";
            return false;
        }
        directory_ = std::filesystem::path(temporary) /
            (L"Apex5IsolationProbe-" + std::to_wstring(GetCurrentProcessId()) +
             L"-" + std::to_wstring(GetTickCount64()));
        std::error_code filesystemError;
        if (!std::filesystem::create_directory(directory_, filesystemError)) {
            error = "Could not create the isolated helper directory (" +
                    filesystemError.message() + ").";
            return false;
        }
        path_ = directory_ / L"Apex5ExternalXInputProbe.exe";
        if (!std::filesystem::copy_file(
                source, path_, std::filesystem::copy_options::none,
                filesystemError)) {
            error = "Could not create the non-whitelisted XInput helper (" +
                    filesystemError.message() + ").";
            cleanup();
            return false;
        }
        return true;
    }

    ~ExternalProbeCopy() { cleanup(); }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    void cleanup() noexcept {
        std::error_code ignored;
        if (!path_.empty()) std::filesystem::remove(path_, ignored);
        if (!directory_.empty()) std::filesystem::remove(directory_, ignored);
    }

    std::filesystem::path directory_;
    std::filesystem::path path_;
};

ExternalXInputSnapshot runExternalXInputSnapshot(
    const std::filesystem::path& helperPath) {
    ExternalXInputSnapshot snapshot{};
    if (helperPath.empty()) {
        snapshot.error = "The non-whitelisted helper path is empty.";
        return snapshot;
    }

    std::wstring commandLine = L"\"" + helperPath.wstring() +
                               L"\" external-xinput-mask";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            nullptr, commandLine.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, helperPath.parent_path().c_str(),
            &startup, &process)) {
        snapshot.error = "Starting the non-whitelisted XInput helper failed (" +
                         std::to_string(GetLastError()) + ").";
        return snapshot;
    }
    snapshot.launched = true;
    CloseHandle(process.hThread);

    const auto wait = WaitForSingleObject(process.hProcess, kExternalProbeTimeoutMs);
    if (wait != WAIT_OBJECT_0) {
        snapshot.error = wait == WAIT_TIMEOUT
            ? "The non-whitelisted XInput helper timed out."
            : "Waiting for the non-whitelisted XInput helper failed (" +
                  std::to_string(GetLastError()) + ").";
        (void)TerminateProcess(process.hProcess, 0xFF);
        (void)WaitForSingleObject(process.hProcess, 2000);
        CloseHandle(process.hProcess);
        return snapshot;
    }

    DWORD exitCode = 0xFFFFFFFFUL;
    if (!GetExitCodeProcess(process.hProcess, &exitCode)) {
        snapshot.error = "Reading the non-whitelisted XInput result failed (" +
                         std::to_string(GetLastError()) + ").";
        CloseHandle(process.hProcess);
        return snapshot;
    }
    CloseHandle(process.hProcess);
    if (exitCode > 0x0F) {
        snapshot.error = "The non-whitelisted XInput helper returned an invalid result.";
        return snapshot;
    }
    snapshot.connectedMask = exitCode;
    return snapshot;
}

int externalXInputMaskCommand() {
    std::uint32_t mask = 0;
    for (const auto slot : asb::platform::connectedXInputGamepads()) {
        if (slot < 4) mask |= (1U << slot);
    }
    return static_cast<int>(mask);
}

class ScopedDeviceInfoSet {
public:
    explicit ScopedDeviceInfoSet(HDEVINFO value) : value_(value) {}
    ~ScopedDeviceInfoSet() {
        if (value_ != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value_);
    }
    ScopedDeviceInfoSet(const ScopedDeviceInfoSet&) = delete;
    ScopedDeviceInfoSet& operator=(const ScopedDeviceInfoSet&) = delete;
    [[nodiscard]] HDEVINFO get() const noexcept { return value_; }

private:
    HDEVINFO value_ = INVALID_HANDLE_VALUE;
};

std::wstring setupDeviceInstanceId(HDEVINFO devices,
                                   SP_DEVINFO_DATA& info) {
    DWORD required = 0;
    SetupDiGetDeviceInstanceIdW(devices, &info, nullptr, 0, &required);
    if (required == 0) return {};
    std::vector<wchar_t> value(required + 1, L'\0');
    if (!SetupDiGetDeviceInstanceIdW(
            devices, &info, value.data(),
            static_cast<DWORD>(value.size()), nullptr)) {
        return {};
    }
    return value.data();
}

bool setupDeviceContainerId(HDEVINFO devices, SP_DEVINFO_DATA& info,
                            GUID& value) {
    DEVPROPTYPE type = 0;
    DWORD required = 0;
    return SetupDiGetDevicePropertyW(
               devices, &info, &DEVPKEY_Device_ContainerId, &type,
               reinterpret_cast<PBYTE>(&value), sizeof(value), &required, 0) &&
           type == DEVPROP_TYPE_GUID && required == sizeof(value);
}

std::wstring setupDeviceStringProperty(HDEVINFO devices,
                                       SP_DEVINFO_DATA& info,
                                       const DEVPROPKEY& key) {
    DEVPROPTYPE type = 0;
    DWORD required = 0;
    SetupDiGetDevicePropertyW(
        devices, &info, &key, &type, nullptr, 0, &required, 0);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
        type != DEVPROP_TYPE_STRING || required < sizeof(wchar_t)) {
        return {};
    }
    std::vector<BYTE> buffer(required + sizeof(wchar_t), 0);
    if (!SetupDiGetDevicePropertyW(
            devices, &info, &key, &type, buffer.data(),
            static_cast<DWORD>(buffer.size()), &required, 0) ||
        type != DEVPROP_TYPE_STRING) {
        return {};
    }
    return reinterpret_cast<const wchar_t*>(buffer.data());
}

bool restartApexXusbStack(const asb::HidDeviceInfo& selected,
                          bool& operatingSystemRestartRequired,
                          std::string& error) {
    operatingSystemRestartRequired = false;
    constexpr GUID xnaCompositeClass{
        0xd61ca365, 0x5af4, 0x4486,
        {0x99, 0x8b, 0x9d, 0xb4, 0x73, 0x4c, 0x6c, 0xa3}};

    const ScopedDeviceInfoSet devices(SetupDiGetClassDevsW(
        nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT));
    if (devices.get() == INVALID_HANDLE_VALUE) {
        error = "Enumerating the Apex 5 PnP stack failed (" +
                std::to_string(GetLastError()) + ").";
        return false;
    }

    GUID selectedContainer{};
    bool selectedFound = false;
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        if (!SetupDiEnumDeviceInfo(devices.get(), index, &info)) break;
        const auto instance = setupDeviceInstanceId(devices.get(), info);
        if (_wcsicmp(instance.c_str(), selected.instanceId.c_str()) == 0) {
            selectedFound = setupDeviceContainerId(
                devices.get(), info, selectedContainer);
            break;
        }
    }
    if (!selectedFound) {
        error = "The selected Apex 5 container disappeared before the stack restart.";
        return false;
    }

    std::wostringstream expectedPrefix;
    expectedPrefix << L"USB\\VID_" << std::hex << std::uppercase
                   << std::setw(4) << std::setfill(L'0') << selected.vendorId
                   << L"&PID_" << std::setw(4) << selected.productId
                   << L"&MI_00\\";

    const auto expectedInstancePrefix = expectedPrefix.str();
    std::optional<SP_DEVINFO_DATA> target;
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        if (!SetupDiEnumDeviceInfo(devices.get(), index, &info)) break;
        GUID container{};
        if (!setupDeviceContainerId(devices.get(), info, container) ||
            !IsEqualGUID(container, selectedContainer) ||
            !IsEqualGUID(info.ClassGuid, xnaCompositeClass)) {
            continue;
        }
        const auto instance = setupDeviceInstanceId(devices.get(), info);
        if (instance.size() < expectedInstancePrefix.size() ||
            _wcsnicmp(instance.c_str(), expectedInstancePrefix.c_str(),
                      expectedInstancePrefix.size()) != 0) {
            continue;
        }
        const auto service = setupDeviceStringProperty(
            devices.get(), info, DEVPKEY_Device_Service);
        if (_wcsicmp(service.c_str(), L"xusb22") != 0) continue;
        if (target) {
            error = "Several verified Apex 5 xusb22 nodes share the selected "
                    "container; refusing an ambiguous stack restart.";
            return false;
        }
        target = info;
    }
    if (!target) {
        error = "The exact Apex 5 USB MI_00 / xusb22 / XnaComposite node was not found.";
        return false;
    }

    SP_PROPCHANGE_PARAMS parameters{};
    parameters.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    parameters.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    parameters.StateChange = DICS_PROPCHANGE;
    parameters.Scope = DICS_FLAG_GLOBAL;
    if (!SetupDiSetClassInstallParamsW(
            devices.get(), &*target, &parameters.ClassInstallHeader,
            sizeof(parameters))) {
        error = "Preparing the exact Apex 5 stack restart failed (" +
                std::to_string(GetLastError()) + ").";
        return false;
    }
    if (!SetupDiCallClassInstaller(
            DIF_PROPERTYCHANGE, devices.get(), &*target)) {
        error = "Restarting the exact Apex 5 xusb22 stack failed (" +
                std::to_string(GetLastError()) + ").";
        return false;
    }

    SP_DEVINSTALL_PARAMS_W installParameters{};
    installParameters.cbSize = sizeof(installParameters);
    if (SetupDiGetDeviceInstallParamsW(
            devices.get(), &*target, &installParameters)) {
        operatingSystemRestartRequired =
            (installParameters.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) != 0;
    }
    return true;
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
    std::optional<InputTransportStatus> finalTransport;
    InputSample baseline;
    InputSample isolated;
    InputSample rebuilt;
    InputSample recovered;
    ExternalXInputSnapshot externalBaseline;
    ExternalXInputSnapshot externalIsolated;
    ExternalXInputSnapshot externalRebuilt;
    ExternalXInputSnapshot externalRecovered;
    bool isolationActivated = false;
    bool stackRestartAttempted = false;
    bool stackRestartSucceeded = false;
    bool operatingSystemRestartRequired = false;
    bool restoreAttempted = false;
    bool restoreSucceeded = false;
    bool transportUnchanged = false;
    bool directHidHideSuccess = false;
    bool rebuiltHidHideSuccess = false;
    bool methodFound = false;
    bool interrupted = false;
    std::string fatalError;
    std::string stackRestartError;
    std::string restoreError;
};

void appendExternalSnapshotJson(
    std::ostringstream& json, const ExternalXInputSnapshot& snapshot,
    unsigned indentation) {
    const std::string spaces(indentation, ' ');
    json << "{\n"
         << spaces << "  \"launched\": "
         << (snapshot.launched ? "true" : "false") << ",\n"
         << spaces << "  \"connected_mask\": "
         << snapshot.connectedMask << ",\n"
         << spaces << "  \"error\": \""
         << jsonEscape(snapshot.error) << "\"\n"
         << spaces << '}';
}

std::string serializeReport(const ProbeReport& report) {
    std::ostringstream json;
    json << "{\n"
         << "  \"probe\": \"Apex5IsolationProbe\",\n"
         << "  \"probe_version\": \"1.1.0\",\n"
         << "  \"generated_utc\": \"" << report.generatedUtc << "\",\n"
         << "  \"device\": {\n"
         << "    \"product\": \"" << jsonEscape(report.product) << "\",\n"
         << "    \"vendor_id\": " << report.vendorId << ",\n"
         << "    \"product_id\": " << report.productId << ",\n"
         << "    \"profile_slot\": " << report.profileSlot << "\n"
         << "  },\n"
         << "  \"original_transport\": ";
    appendTransportJson(json, report.originalTransport, 2);
    json << ",\n  \"final_transport\": ";
    appendTransportJson(json, report.finalTransport, 2);
    json << ",\n  \"baseline\": ";
    appendSampleJson(json, report.baseline, 2);
    json << ",\n  \"isolated\": ";
    appendSampleJson(json, report.isolated, 2);
    json << ",\n  \"rebuilt\": ";
    appendSampleJson(json, report.rebuilt, 2);
    json << ",\n  \"recovered\": ";
    appendSampleJson(json, report.recovered, 2);
    json << ",\n  \"external_xinput\": {\n"
         << "    \"baseline\": ";
    appendExternalSnapshotJson(json, report.externalBaseline, 4);
    json << ",\n    \"isolated\": ";
    appendExternalSnapshotJson(json, report.externalIsolated, 4);
    json << ",\n    \"rebuilt\": ";
    appendExternalSnapshotJson(json, report.externalRebuilt, 4);
    json << ",\n    \"recovered\": ";
    appendExternalSnapshotJson(json, report.externalRecovered, 4);
    json << "\n  },\n"
         << "  \"safety\": {\n"
         << "    \"isolation_activated\": "
         << (report.isolationActivated ? "true" : "false") << ",\n"
         << "    \"stack_restart_attempted\": "
         << (report.stackRestartAttempted ? "true" : "false") << ",\n"
         << "    \"stack_restart_succeeded\": "
         << (report.stackRestartSucceeded ? "true" : "false") << ",\n"
         << "    \"os_restart_required\": "
         << (report.operatingSystemRestartRequired ? "true" : "false") << ",\n"
         << "    \"restore_attempted\": "
         << (report.restoreAttempted ? "true" : "false") << ",\n"
         << "    \"restore_succeeded\": "
         << (report.restoreSucceeded ? "true" : "false") << ",\n"
         << "    \"interrupted\": "
         << (report.interrupted ? "true" : "false") << "\n"
         << "  },\n"
         << "  \"verdict\": {\n"
         << "    \"transport_unchanged\": "
         << (report.transportUnchanged ? "true" : "false") << ",\n"
         << "    \"direct_hidhide_success\": "
         << (report.directHidHideSuccess ? "true" : "false") << ",\n"
         << "    \"stack_rebuild_success\": "
         << (report.rebuiltHidHideSuccess ? "true" : "false") << ",\n"
         << "    \"method_found\": "
         << (report.methodFound ? "true" : "false") << "\n"
         << "  },\n"
         << "  \"fatal_error\": \"" << jsonEscape(report.fatalError)
         << "\",\n"
         << "  \"stack_restart_error\": \""
         << jsonEscape(report.stackRestartError) << "\",\n"
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
        << "Apex 5 HidHide/XInput stack probe v1.1\n\n"
        << "Before running: close ApexSenseBridge, Flydigi Space Station, games, "
           "and Steam; in HidHide, "
           "uncheck 'Enable device hiding', then close the HidHide client. "
           "Disconnect every other gamepad.\n"
        << "The probe preserves the existing HidHide lists and restores its "
           "original enabled/disabled state.\n"
        << "It does not change the controller firmware routing. A copied helper "
           "tests XInput from a path that is deliberately not whitelisted.\n"
        << "Only if XInput still leaks, the probe can request a non-persistent "
           "stop/start of the exact verified Apex 5 MI_00 xusb22 device stack.\n"
        << "Move both sticks, press both triggers, the D-pad, and all face/shoulder "
           "buttons during each prompted phase.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2) {
        const std::string_view command = argv[1];
        if (command == "external-xinput-mask") {
            return externalXInputMaskCommand();
        }
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
    ExternalProbeCopy externalProbe;
    int exitCode = 1;

    const auto finish = [&report, &exitCode, &isolation]() {
        if (isolation.active()) {
            report.restoreAttempted = true;
            report.restoreSucceeded = isolation.restore(report.restoreError);
            if (!report.restoreSucceeded && report.fatalError.empty()) {
                report.fatalError = "Automatic HidHide restoration failed.";
            }
        }
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
        << "\nIMPORTANT: The firmware routing is never modified. HidHide recovery "
           "is guarded by a watchdog and RunOnce marker. The optional PnP action "
           "is DICS_PROPCHANGE (stop/start), not a persistent device disable.\n\n";
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

    error.clear();
    if (!externalProbe.create(error)) {
        report.fatalError = error;
        std::cerr << report.fatalError << '\n';
        return finish();
    }

    if (!waitForEnter(
            "\nBASELINE (8 seconds): press ENTER, then move every control...")) {
        report.fatalError = "Canceled before the baseline sample.";
        return finish();
    }
    report.baseline = collectInputSample(selectedInfo, kSampleSeconds);
    report.externalBaseline = runExternalXInputSnapshot(externalProbe.path());
    if (!report.externalBaseline.error.empty()) {
        report.fatalError = report.externalBaseline.error;
        std::cerr << report.fatalError << '\n';
        return finish();
    }
    if (report.externalBaseline.connectedMask == 0) {
        report.fatalError =
            "The non-whitelisted baseline saw no XInput controller; the test "
            "cannot distinguish successful hiding from a missing source.";
        std::cerr << report.fatalError << '\n';
        return finish();
    }

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
    report.externalIsolated = runExternalXInputSnapshot(externalProbe.path());
    if (!report.externalIsolated.error.empty()) {
        report.fatalError = report.externalIsolated.error;
        std::cerr << report.fatalError << '\n';
        device.reset();
        return finish();
    }

    if (!waitForEnter(
            "\nHIDHIDE ACTIVE (8 seconds): press ENTER, then repeat every control...")) {
        report.fatalError = "Canceled before the isolated input sample.";
        device.reset();
        return finish();
    }
    report.isolated = collectInputSample(selectedInfo, kSampleSeconds);
    report.directHidHideSuccess =
        report.externalIsolated.connectedMask == 0 &&
        report.isolated.stats.mappedReports > 0;

    if (!report.directHidHideSuccess &&
        report.externalIsolated.connectedMask != 0) {
        std::cout
            << "\nThe external process still sees XInput. The next action only "
               "restarts the exact verified USB MI_00 / xusb22 / XnaComposite "
               "node so HidHide can attach to its newly built stack. It does not "
               "persistently disable the controller.\n";
        if (!waitForEnter(
                "Press ENTER to perform the controlled stack restart, or Ctrl+C to cancel...")) {
            report.fatalError = "Canceled before the controlled xusb22 stack restart.";
            device.reset();
            return finish();
        }

        device.reset();
        report.stackRestartAttempted = true;
        report.stackRestartSucceeded = restartApexXusbStack(
            selectedInfo, report.operatingSystemRestartRequired,
            report.stackRestartError);
        if (!report.stackRestartSucceeded) {
            report.fatalError = "The controlled xusb22 stack restart failed: " +
                                report.stackRestartError;
            std::cerr << report.fatalError << '\n';
        } else if (report.operatingSystemRestartRequired) {
            report.fatalError =
                "Windows accepted the stack change but requires a system restart; "
                "the live result cannot be validated safely.";
        } else {
            std::optional<Apex5Device> reopened;
            const auto reopenDeadline = std::chrono::steady_clock::now() +
                                        std::chrono::seconds(10);
            do {
                error.clear();
                reopened = openOnlyApex5(selectedInfo, error);
                if (!reopened) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            } while (!reopened &&
                     std::chrono::steady_clock::now() < reopenDeadline &&
                     !gStopRequested.load(std::memory_order_relaxed));

            if (!reopened) {
                report.fatalError =
                    "The Apex 5 did not reopen after the controlled stack restart: " +
                    error;
            } else {
                device = std::move(reopened);
                report.externalRebuilt =
                    runExternalXInputSnapshot(externalProbe.path());
                if (!report.externalRebuilt.error.empty()) {
                    report.fatalError = report.externalRebuilt.error;
                } else if (waitForEnter(
                               "\nREBUILT STACK (8 seconds): press ENTER, then repeat every control...")) {
                    report.rebuilt = collectInputSample(
                        selectedInfo, kSampleSeconds);
                    report.rebuiltHidHideSuccess =
                        report.externalRebuilt.connectedMask == 0 &&
                        report.rebuilt.stats.mappedReports > 0;
                } else {
                    report.fatalError =
                        "Canceled before the rebuilt-stack input sample.";
                }
            }
        }
    }

    device.reset();
    report.restoreAttempted = true;
    report.restoreSucceeded = isolation.restore(report.restoreError);
    if (!report.restoreSucceeded && report.fatalError.empty()) {
        report.fatalError = "Automatic HidHide restoration failed.";
    }

    if (report.restoreSucceeded) {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        report.externalRecovered =
            runExternalXInputSnapshot(externalProbe.path());
        auto restoredDevice = openOnlyApex5(selectedInfo, error);
        if (restoredDevice) {
            InputTransportStatus finalTransport{};
            if (restoredDevice->readInputTransportStatus(finalTransport, error)) {
                report.finalTransport = finalTransport;
                report.transportUnchanged =
                    sameTransport(original, finalTransport);
            } else if (report.fatalError.empty()) {
                report.fatalError =
                    "Could not verify the unchanged input routing: " + error;
            }
            restoredDevice.reset();
            report.recovered = collectInputSample(selectedInfo, 3);
        } else if (report.fatalError.empty()) {
            report.fatalError = "Could not reopen the restored Apex 5: " + error;
        }
    }

    const bool externalVisibilityRestored =
        report.externalRecovered.error.empty() &&
        report.externalRecovered.connectedMask ==
            report.externalBaseline.connectedMask;
    report.methodFound =
        (report.directHidHideSuccess || report.rebuiltHidHideSuccess) &&
        report.restoreSucceeded && report.transportUnchanged &&
        externalVisibilityRestored;
    exitCode = report.methodFound && report.fatalError.empty() ? 0 : 7;

    std::cout
        << "\nResult: "
        << (report.directHidHideSuccess
                ? "HidHide blocks external XInput without a stack restart."
                : report.rebuiltHidHideSuccess
                    ? "the controlled xusb22 stack restart makes HidHide block "
                      "external XInput while mapped HID stays available."
                    : "no safe isolation method was proven; no persistent "
                      "device change was made.")
        << '\n';
    return finish();
}
