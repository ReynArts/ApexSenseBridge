#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>

#include "platform/PhysicalControllerIsolation.h"

#include "flydigi/Apex4Protocol.h"
#include "platform/SessionControl.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace asb::platform {
namespace {

constexpr wchar_t kHidHideDevice[] = L"\\\\.\\HidHide";
constexpr wchar_t kRecoveryKey[] =
    L"Software\\ApexSenseBridge\\PhysicalControllerIsolation";
constexpr wchar_t kRunOnceKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
constexpr wchar_t kRunOnceValue[] =
    L"!ApexSenseBridgeRestoreControllerVisibility";
constexpr DWORD kRecoveryVersion = 1;
constexpr DWORD kPhasePrepared = 0;
constexpr DWORD kPhaseConfigurationMayHaveChanged = 1;
constexpr wchar_t kUninstallKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
constexpr wchar_t kSpaceStationDisplayName[] = L"Flydigi Space Station";
constexpr wchar_t kSpaceStationPublisher[] = L"Flydigi";
constexpr wchar_t kSpaceStationService[] = L"SpaceStationService.exe";

constexpr DWORD kIoctlGetWhitelist =
    static_cast<DWORD>(CTL_CODE(32769, 2048, METHOD_BUFFERED, FILE_READ_DATA));
constexpr DWORD kIoctlSetWhitelist =
    static_cast<DWORD>(CTL_CODE(32769, 2049, METHOD_BUFFERED, FILE_READ_DATA));
constexpr DWORD kIoctlGetBlacklist =
    static_cast<DWORD>(CTL_CODE(32769, 2050, METHOD_BUFFERED, FILE_READ_DATA));
constexpr DWORD kIoctlSetBlacklist =
    static_cast<DWORD>(CTL_CODE(32769, 2051, METHOD_BUFFERED, FILE_READ_DATA));
constexpr DWORD kIoctlGetActive =
    static_cast<DWORD>(CTL_CODE(32769, 2052, METHOD_BUFFERED, FILE_READ_DATA));
constexpr DWORD kIoctlSetActive =
    static_cast<DWORD>(CTL_CODE(32769, 2053, METHOD_BUFFERED, FILE_READ_DATA));
constexpr DWORD kIoctlGetInverse =
    static_cast<DWORD>(CTL_CODE(32769, 2054, METHOD_BUFFERED, FILE_READ_DATA));
constexpr DWORD kIoctlSetInverse =
    static_cast<DWORD>(CTL_CODE(32769, 2055, METHOD_BUFFERED, FILE_READ_DATA));

class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept
        : handle_(handle) {}
    ~ScopedHandle() {
        if (valid()) CloseHandle(handle_);
    }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    void reset(HANDLE handle = INVALID_HANDLE_VALUE) noexcept {
        if (valid()) CloseHandle(handle_);
        handle_ = handle;
    }

private:
    HANDLE handle_;
};

class ScopedDeviceInfoSet {
public:
    explicit ScopedDeviceInfoSet(HDEVINFO value) noexcept : value_(value) {}
    ~ScopedDeviceInfoSet() {
        if (value_ != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value_);
    }
    ScopedDeviceInfoSet(const ScopedDeviceInfoSet&) = delete;
    ScopedDeviceInfoSet& operator=(const ScopedDeviceInfoSet&) = delete;
    [[nodiscard]] HDEVINFO get() const noexcept { return value_; }

private:
    HDEVINFO value_;
};

class ScopedRegistryKey {
public:
    explicit ScopedRegistryKey(HKEY value = nullptr) noexcept : value_(value) {}
    ~ScopedRegistryKey() {
        if (value_) RegCloseKey(value_);
    }
    ScopedRegistryKey(const ScopedRegistryKey&) = delete;
    ScopedRegistryKey& operator=(const ScopedRegistryKey&) = delete;
    [[nodiscard]] HKEY get() const noexcept { return value_; }

private:
    HKEY value_;
};

struct RecoverySnapshot {
    DWORD ownerProcessId = 0;
    DWORD phase = kPhasePrepared;
    bool originalActive = false;
    bool originalInverse = false;
    std::vector<std::wstring> originalWhitelist;
    std::vector<std::wstring> originalBlacklist;
};

std::string windowsError(std::string_view operation, DWORD code) {
    return std::string(operation) + " failed (Windows error " +
           std::to_string(code) + ')';
}

std::wstring upper(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towupper(character));
    });
    return value;
}

bool startsWithCaseInsensitive(std::wstring_view value,
                               std::wstring_view prefix) noexcept {
    if (value.size() < prefix.size()) return false;
    return _wcsnicmp(value.data(), prefix.data(), prefix.size()) == 0;
}

bool hasPrefixBoundary(std::wstring_view value,
                       std::wstring_view prefix) noexcept {
    if (!startsWithCaseInsensitive(value, prefix)) return false;
    if (value.size() == prefix.size()) return true;
    const auto next = value[prefix.size()];
    return std::iswspace(next) || next == L',' || next == L'.';
}

bool containsCaseInsensitive(const std::vector<std::wstring>& values,
                             const std::wstring& wanted) {
    return std::any_of(values.begin(), values.end(), [&wanted](const auto& value) {
        return _wcsicmp(value.c_str(), wanted.c_str()) == 0;
    });
}

std::vector<wchar_t> toMultiString(const std::vector<std::wstring>& values) {
    std::vector<wchar_t> buffer;
    for (const auto& value : values) {
        if (value.empty() || value.find(L'\0') != std::wstring::npos) continue;
        buffer.insert(buffer.end(), value.begin(), value.end());
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0');
    return buffer;
}

std::vector<std::wstring> fromMultiString(const wchar_t* data,
                                          std::size_t characters) {
    std::vector<std::wstring> values;
    std::size_t start = 0;
    for (std::size_t index = 0; index < characters; ++index) {
        if (data[index] != L'\0') continue;
        if (index > start) values.emplace_back(data + start, index - start);
        start = index + 1;
    }
    return values;
}

bool openHidHide(ScopedHandle& device, std::string& error) {
    const HANDLE handle = CreateFileW(
        kHidHideDevice, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
            error = "HidHide is not installed or Windows has not been restarted "
                    "since its installation.";
        } else {
            error = windowsError("Opening the HidHide control device", code);
        }
        return false;
    }
    device.reset(handle);
    return true;
}

bool getBoolean(HANDLE device, DWORD controlCode, bool& value,
                std::string_view label, std::string& error) {
    unsigned char raw = 0;
    DWORD returned = 0;
    if (!DeviceIoControl(device, controlCode, nullptr, 0, &raw, sizeof(raw),
                         &returned, nullptr) || returned != sizeof(raw)) {
        error = windowsError(std::string("Reading HidHide ") + std::string(label),
                             GetLastError());
        return false;
    }
    value = raw != 0;
    return true;
}

bool setBoolean(HANDLE device, DWORD controlCode, bool value,
                std::string_view label, std::string& error) {
    unsigned char raw = value ? 1 : 0;
    DWORD returned = 0;
    if (!DeviceIoControl(device, controlCode, &raw, sizeof(raw), nullptr, 0,
                         &returned, nullptr)) {
        error = windowsError(std::string("Writing HidHide ") + std::string(label),
                             GetLastError());
        return false;
    }
    return true;
}

bool getList(HANDLE device, DWORD controlCode,
             std::vector<std::wstring>& values,
             std::string_view label, std::string& error) {
    DWORD requiredBytes = 0;
    if (!DeviceIoControl(device, controlCode, nullptr, 0, nullptr, 0,
                         &requiredBytes, nullptr)) {
        error = windowsError(std::string("Sizing HidHide ") + std::string(label),
                             GetLastError());
        return false;
    }
    if (requiredBytes == 0 || requiredBytes > 1024 * 1024 ||
        requiredBytes % sizeof(wchar_t) != 0) {
        error = "HidHide returned an invalid " + std::string(label) + " size.";
        return false;
    }
    std::vector<wchar_t> buffer(requiredBytes / sizeof(wchar_t), L'\0');
    DWORD returned = 0;
    if (!DeviceIoControl(device, controlCode, nullptr, 0, buffer.data(),
                         requiredBytes, &returned, nullptr) ||
        returned > requiredBytes || returned % sizeof(wchar_t) != 0) {
        error = windowsError(std::string("Reading HidHide ") + std::string(label),
                             GetLastError());
        return false;
    }
    values = fromMultiString(buffer.data(), returned / sizeof(wchar_t));
    return true;
}

bool setList(HANDLE device, DWORD controlCode,
             const std::vector<std::wstring>& values,
             std::string_view label, std::string& error) {
    const auto buffer = toMultiString(values);
    const auto byteSize = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    DWORD returned = 0;
    if (!DeviceIoControl(device, controlCode,
                         const_cast<wchar_t*>(buffer.data()), byteSize,
                         nullptr, 0, &returned, nullptr)) {
        error = windowsError(std::string("Writing HidHide ") + std::string(label),
                             GetLastError());
        return false;
    }
    return true;
}

bool setRegistryDword(HKEY key, const wchar_t* name, DWORD value,
                      std::string& error) {
    const auto status = RegSetValueExW(
        key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    if (status != ERROR_SUCCESS) {
        error = windowsError("Writing the controller recovery marker", status);
        return false;
    }
    return true;
}

bool getRegistryDword(HKEY key, const wchar_t* name, DWORD& value,
                      std::string& error) {
    DWORD type = 0;
    DWORD size = sizeof(value);
    const auto status = RegQueryValueExW(
        key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size);
    if (status != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(value)) {
        error = "The controller recovery marker is incomplete or corrupt.";
        return false;
    }
    return true;
}

bool setRegistryList(HKEY key, const wchar_t* name,
                     const std::vector<std::wstring>& values,
                     std::string& error) {
    const auto buffer = toMultiString(values);
    const auto status = RegSetValueExW(
        key, name, 0, REG_MULTI_SZ,
        reinterpret_cast<const BYTE*>(buffer.data()),
        static_cast<DWORD>(buffer.size() * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS) {
        error = windowsError("Writing the controller recovery lists", status);
        return false;
    }
    return true;
}

bool getRegistryList(HKEY key, const wchar_t* name,
                     std::vector<std::wstring>& values,
                     std::string& error) {
    DWORD type = 0;
    DWORD size = 0;
    auto status = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS || type != REG_MULTI_SZ || size == 0 ||
        size > 1024 * 1024 || size % sizeof(wchar_t) != 0) {
        error = "The controller recovery lists are incomplete or corrupt.";
        return false;
    }
    std::vector<wchar_t> buffer(size / sizeof(wchar_t), L'\0');
    status = RegQueryValueExW(key, name, nullptr, &type,
                              reinterpret_cast<BYTE*>(buffer.data()), &size);
    if (status != ERROR_SUCCESS) {
        error = windowsError("Reading the controller recovery lists", status);
        return false;
    }
    values = fromMultiString(buffer.data(), size / sizeof(wchar_t));
    return true;
}

bool recoveryMarkerExists() {
    HKEY key = nullptr;
    const auto status = RegOpenKeyExW(
        HKEY_CURRENT_USER, kRecoveryKey, 0, KEY_QUERY_VALUE, &key);
    if (status == ERROR_SUCCESS) RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool writeRecoverySnapshot(const RecoverySnapshot& snapshot, std::string& error) {
    HKEY rawKey = nullptr;
    DWORD disposition = 0;
    const auto status = RegCreateKeyExW(
        HKEY_CURRENT_USER, kRecoveryKey, 0, nullptr, 0,
        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &rawKey, &disposition);
    if (status != ERROR_SUCCESS) {
        error = windowsError("Creating the controller recovery marker", status);
        return false;
    }
    ScopedRegistryKey key(rawKey);
    if (disposition != REG_CREATED_NEW_KEY) {
        error = "A controller recovery marker already exists.";
        return false;
    }
    if (!setRegistryDword(key.get(), L"OwnerProcessId", snapshot.ownerProcessId, error) ||
        !setRegistryDword(key.get(), L"Phase", snapshot.phase, error) ||
        !setRegistryDword(key.get(), L"OriginalActive", snapshot.originalActive ? 1 : 0, error) ||
        !setRegistryDword(key.get(), L"OriginalInverse", snapshot.originalInverse ? 1 : 0, error) ||
        !setRegistryList(key.get(), L"OriginalWhitelist", snapshot.originalWhitelist, error) ||
        !setRegistryList(key.get(), L"OriginalBlacklist", snapshot.originalBlacklist, error) ||
        !setRegistryDword(key.get(), L"Version", kRecoveryVersion, error)) {
        RegDeleteTreeW(HKEY_CURRENT_USER, kRecoveryKey);
        return false;
    }
    return true;
}

bool readRecoverySnapshot(RecoverySnapshot& snapshot, bool& exists,
                          std::string& error) {
    exists = false;
    HKEY rawKey = nullptr;
    const auto status = RegOpenKeyExW(
        HKEY_CURRENT_USER, kRecoveryKey, 0, KEY_QUERY_VALUE, &rawKey);
    if (status == ERROR_FILE_NOT_FOUND) return true;
    if (status != ERROR_SUCCESS) {
        error = windowsError("Opening the controller recovery marker", status);
        return false;
    }
    exists = true;
    ScopedRegistryKey key(rawKey);
    DWORD version = 0;
    DWORD active = 0;
    DWORD inverse = 0;
    if (!getRegistryDword(key.get(), L"Version", version, error) ||
        version != kRecoveryVersion ||
        !getRegistryDword(key.get(), L"OwnerProcessId", snapshot.ownerProcessId, error) ||
        !getRegistryDword(key.get(), L"Phase", snapshot.phase, error) ||
        !getRegistryDword(key.get(), L"OriginalActive", active, error) ||
        !getRegistryDword(key.get(), L"OriginalInverse", inverse, error) ||
        !getRegistryList(key.get(), L"OriginalWhitelist", snapshot.originalWhitelist, error) ||
        !getRegistryList(key.get(), L"OriginalBlacklist", snapshot.originalBlacklist, error)) {
        if (error.empty()) error = "The controller recovery marker version is unsupported.";
        return false;
    }
    if (active > 1 || inverse > 1 ||
        (snapshot.phase != kPhasePrepared &&
         snapshot.phase != kPhaseConfigurationMayHaveChanged)) {
        error = "The controller recovery marker contains invalid values.";
        return false;
    }
    snapshot.originalActive = active != 0;
    snapshot.originalInverse = inverse != 0;
    return true;
}

void clearRecoveryRegistration() noexcept {
    HKEY rawRunOnce = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunOnceKey, 0, KEY_SET_VALUE,
                      &rawRunOnce) == ERROR_SUCCESS) {
        RegDeleteValueW(rawRunOnce, kRunOnceValue);
        RegCloseKey(rawRunOnce);
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, kRecoveryKey);
}

std::wstring moduleFileName(std::string& error) {
    std::vector<wchar_t> buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        error = windowsError("Resolving the ApexSenseBridge executable", GetLastError());
        return {};
    }
    return std::wstring(buffer.data(), length);
}

bool registerRunOnce(const std::wstring& executable, std::string& error) {
    HKEY rawKey = nullptr;
    const auto status = RegCreateKeyExW(
        HKEY_CURRENT_USER, kRunOnceKey, 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &rawKey, nullptr);
    if (status != ERROR_SUCCESS) {
        error = windowsError("Registering controller recovery at next login", status);
        return false;
    }
    ScopedRegistryKey key(rawKey);
    const std::wstring command =
        L"\"" + executable + L"\" restore-controller-visibility";
    const auto setStatus = RegSetValueExW(
        key.get(), kRunOnceValue, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    if (setStatus != ERROR_SUCCESS) {
        error = windowsError("Registering controller recovery at next login", setStatus);
        return false;
    }
    return true;
}

bool setRecoveryPhase(DWORD phase, std::string& error) {
    HKEY rawKey = nullptr;
    const auto status = RegOpenKeyExW(
        HKEY_CURRENT_USER, kRecoveryKey, 0, KEY_SET_VALUE, &rawKey);
    if (status != ERROR_SUCCESS) {
        error = windowsError("Updating the controller recovery marker", status);
        return false;
    }
    ScopedRegistryKey key(rawKey);
    return setRegistryDword(key.get(), L"Phase", phase, error);
}

bool processIsRunning(DWORD processId) noexcept {
    if (processId == 0) return false;
    ScopedHandle process(OpenProcess(SYNCHRONIZE, FALSE, processId));
    if (!process.valid()) {
        return GetLastError() != ERROR_INVALID_PARAMETER;
    }
    return WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT;
}

bool startWatchdog(const std::wstring& executable, DWORD processId,
                   std::string_view sessionToken,
                   std::string& error) {
    std::wstring command = L"\"" + executable + L"\" hidhide-watchdog " +
                           std::to_wstring(processId);
    if (!sessionToken.empty()) {
        command += L" " + std::wstring(sessionToken.begin(), sessionToken.end());
    }
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), commandBuffer.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &process)) {
        error = windowsError("Starting the controller recovery watchdog", GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

std::wstring imageNtPath(const std::wstring& executable,
                         std::string& error) {
    if (executable.size() < 3 || executable[1] != L':') {
        error = "HidHide isolation currently requires ApexSenseBridge to run "
                "from a drive-letter path.";
        return {};
    }
    const std::wstring drive = executable.substr(0, 2);
    std::array<wchar_t, 32768> devicePath{};
    if (QueryDosDeviceW(drive.c_str(), devicePath.data(),
                        static_cast<DWORD>(devicePath.size())) == 0) {
        error = windowsError("Converting the bridge path for HidHide", GetLastError());
        return {};
    }
    return std::wstring(devicePath.data()) + executable.substr(2);
}

bool readRegistryString(HKEY key, const wchar_t* name,
                        std::wstring& value) {
    DWORD type = 0;
    DWORD bytes = 0;
    auto status = RegQueryValueExW(
        key, name, nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) ||
        bytes < sizeof(wchar_t) || bytes > 64 * 1024 ||
        bytes % sizeof(wchar_t) != 0) {
        return false;
    }

    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    status = RegQueryValueExW(
        key, name, nullptr, &type,
        reinterpret_cast<BYTE*>(buffer.data()), &bytes);
    if (status != ERROR_SUCCESS) return false;
    value.assign(buffer.data());

    if (type == REG_EXPAND_SZ) {
        const auto required = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (required == 0 || required > 32768) return false;
        std::vector<wchar_t> expanded(required, L'\0');
        if (ExpandEnvironmentStringsW(
                value.c_str(), expanded.data(), required) != required) {
            return false;
        }
        value.assign(expanded.data());
    }
    return !value.empty();
}

void appendSpaceStationServicesFromRegistry(
    HKEY hive, REGSAM registryView,
    std::vector<std::wstring>& ntPaths) {
    HKEY rawRoot = nullptr;
    if (RegOpenKeyExW(hive, kUninstallKey, 0,
                      KEY_ENUMERATE_SUB_KEYS | registryView,
                      &rawRoot) != ERROR_SUCCESS) {
        return;
    }
    ScopedRegistryKey root(rawRoot);

    for (DWORD index = 0;; ++index) {
        std::array<wchar_t, 256> subkeyName{};
        DWORD nameLength = static_cast<DWORD>(subkeyName.size());
        const auto enumStatus = RegEnumKeyExW(
            root.get(), index, subkeyName.data(), &nameLength,
            nullptr, nullptr, nullptr, nullptr);
        if (enumStatus == ERROR_NO_MORE_ITEMS) break;
        if (enumStatus != ERROR_SUCCESS) continue;

        HKEY rawEntry = nullptr;
        if (RegOpenKeyExW(root.get(), subkeyName.data(), 0,
                          KEY_QUERY_VALUE | registryView,
                          &rawEntry) != ERROR_SUCCESS) {
            continue;
        }
        ScopedRegistryKey entry(rawEntry);
        std::wstring displayName;
        std::wstring publisher;
        std::wstring installLocation;
        if (!readRegistryString(entry.get(), L"DisplayName", displayName) ||
            !readRegistryString(entry.get(), L"Publisher", publisher) ||
            !detail::matchesFlydigiSpaceStationInstall(
                displayName, publisher) ||
            !readRegistryString(
                entry.get(), L"InstallLocation", installLocation)) {
            continue;
        }

        const auto service =
            (std::filesystem::path(installLocation) /
             kSpaceStationService).wstring();
        const auto attributes = GetFileAttributesW(service.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }

        std::string ignored;
        const auto ntPath = imageNtPath(service, ignored);
        if (!ntPath.empty() &&
            !containsCaseInsensitive(ntPaths, ntPath)) {
            ntPaths.push_back(ntPath);
        }
    }
}

std::vector<std::wstring> flydigiSpaceStationServiceNtPaths() {
    std::vector<std::wstring> paths;
    // Space Station has shipped as both a machine-wide and per-user install.
    // Query both registry views because its installer architecture has also
    // changed between releases.
    for (const auto view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        appendSpaceStationServicesFromRegistry(
            HKEY_LOCAL_MACHINE, view, paths);
        appendSpaceStationServicesFromRegistry(
            HKEY_CURRENT_USER, view, paths);
    }
    return paths;
}

std::wstring deviceInstanceId(HDEVINFO devices, SP_DEVINFO_DATA& info) {
    DWORD required = 0;
    SetupDiGetDeviceInstanceIdW(devices, &info, nullptr, 0, &required);
    if (required == 0) return {};
    std::vector<wchar_t> buffer(required + 1, L'\0');
    if (!SetupDiGetDeviceInstanceIdW(
            devices, &info, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr)) {
        return {};
    }
    return buffer.data();
}

bool deviceContainerId(HDEVINFO devices, SP_DEVINFO_DATA& info, GUID& container) {
    DEVPROPTYPE type = 0;
    DWORD required = 0;
    return SetupDiGetDevicePropertyW(
               devices, &info, &DEVPKEY_Device_ContainerId, &type,
               reinterpret_cast<PBYTE>(&container), sizeof(container),
               &required, 0) != FALSE &&
           type == DEVPROP_TYPE_GUID && required == sizeof(container);
}

bool apexGameDevicePaths(const HidDeviceInfo& apexInterface,
                         std::vector<std::wstring>& paths,
                         std::string& error) {
    const ScopedDeviceInfoSet devices(SetupDiGetClassDevsW(
        nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT));
    if (devices.get() == INVALID_HANDLE_VALUE) {
        error = windowsError("Enumerating devices for APEX isolation", GetLastError());
        return false;
    }

    GUID selectedContainer{};
    bool selectedFound = false;
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        if (!SetupDiEnumDeviceInfo(devices.get(), index, &info)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) {
                error = windowsError("Enumerating devices for APEX isolation", GetLastError());
                return false;
            }
            break;
        }
        const auto instance = deviceInstanceId(devices.get(), info);
        if (_wcsicmp(instance.c_str(), apexInterface.instanceId.c_str()) == 0) {
            if (!deviceContainerId(devices.get(), info, selectedContainer)) {
                error = "Windows did not expose a container ID for the selected APEX.";
                return false;
            }
            selectedFound = true;
            break;
        }
    }
    if (!selectedFound) {
        error = "The selected APEX device instance disappeared before isolation.";
        return false;
    }

    std::wostringstream id;
    id << L"VID_" << std::hex << std::uppercase << std::setw(4)
       << std::setfill(L'0') << apexInterface.vendorId << L"&PID_"
       << std::setw(4) << apexInterface.productId;
    const auto expectedId = id.str();
    const bool apex4DInput = flydigi::isApex4Product(
        apexInterface.vendorId, apexInterface.productId);
    bool foundHidGamepad = false;
    bool foundUsbGameInterface = false;
    bool foundHidAuxiliaryInput = !apex4DInput;
    bool foundUsbAuxiliaryInput = !apex4DInput;

    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        if (!SetupDiEnumDeviceInfo(devices.get(), index, &info)) break;
        GUID container{};
        if (!deviceContainerId(devices.get(), info, container) ||
            !IsEqualGUID(container, selectedContainer)) {
            continue;
        }
        const auto instance = deviceInstanceId(devices.get(), info);
        const auto normalized = upper(instance);
        if (normalized.find(expectedId) == std::wstring::npos) continue;
        const bool apex4Gamepad = apex4DInput &&
                                  normalized.find(L"&MI_00") != std::wstring::npos;
        // APEX 4 DInput also exposes MI_01 as a standard HID mouse. Leaving
        // that collection visible lets mapped trigger/mouse input reach a game
        // beside the virtual DualSense, producing mixed input and sticky aim.
        // MI_02/MI_03 remain visible so the bridge can keep using the vendor
        // input, FORCEADAPT and rumble transports.
        const bool apex4AuxiliaryInput = apex4DInput &&
                                         normalized.find(L"&MI_01") != std::wstring::npos;
        const bool hidGamepad = normalized.starts_with(L"HID\\") &&
            (apex4DInput
                 ? apex4Gamepad || apex4AuxiliaryInput
                 : normalized.find(L"&IG_") != std::wstring::npos);
        const bool usbGameInterface = normalized.starts_with(L"USB\\") &&
            (normalized.find(L"&MI_00") != std::wstring::npos ||
             apex4AuxiliaryInput);
        if (hidGamepad || usbGameInterface) {
            if (!containsCaseInsensitive(paths, instance)) paths.push_back(instance);
            foundHidGamepad = foundHidGamepad ||
                              (hidGamepad && (!apex4DInput || apex4Gamepad));
            foundUsbGameInterface = foundUsbGameInterface ||
                                    (usbGameInterface &&
                                     (!apex4DInput || apex4Gamepad));
            foundHidAuxiliaryInput = foundHidAuxiliaryInput ||
                                     (hidGamepad && apex4AuxiliaryInput);
            foundUsbAuxiliaryInput = foundUsbAuxiliaryInput ||
                                     (usbGameInterface && apex4AuxiliaryInput);
        }
    }
    if (!foundHidGamepad || !foundUsbGameInterface ||
        !foundHidAuxiliaryInput || !foundUsbAuxiliaryInput) {
        error = apex4DInput
            ? "Could not identify the complete APEX 4 MI_00 gamepad and MI_01 "
              "auxiliary-input HID/USB set; refusing to start with partial isolation."
            : "Could not identify both HID and USB game interfaces of the selected "
              "APEX; refusing to hide a broader device group.";
        return false;
    }
    return true;
}

bool recoverPendingImpl(bool& recovered, std::string& error) {
    recovered = false;
    RecoverySnapshot snapshot{};
    bool exists = false;
    if (!readRecoverySnapshot(snapshot, exists, error)) return false;
    if (!exists) return true;

    if (snapshot.phase == kPhasePrepared) {
        clearRecoveryRegistration();
        recovered = true;
        return true;
    }

    ScopedHandle device;
    if (!openHidHide(device, error)) return false;

    // Visibility is restored first. Even if a later configuration write
    // fails, games immediately regain access to the physical controller.
    if (!setBoolean(device.get(), kIoctlSetActive, false, "active state", error) ||
        !setList(device.get(), kIoctlSetBlacklist, snapshot.originalBlacklist,
                 "device list", error) ||
        !setList(device.get(), kIoctlSetWhitelist, snapshot.originalWhitelist,
                 "application list", error) ||
        !setBoolean(device.get(), kIoctlSetInverse, snapshot.originalInverse,
                    "inverse state", error) ||
        !setBoolean(device.get(), kIoctlSetActive, snapshot.originalActive,
                    "active state", error)) {
        return false;
    }
    clearRecoveryRegistration();
    recovered = true;
    return true;
}

} // namespace

struct TemporaryPhysicalControllerIsolation::Impl {
    bool active = false;
    bool recovered = false;
};

TemporaryPhysicalControllerIsolation::TemporaryPhysicalControllerIsolation()
    : impl_(std::make_unique<Impl>()) {}

TemporaryPhysicalControllerIsolation::~TemporaryPhysicalControllerIsolation() {
    std::string ignored;
    (void)restore(ignored);
}

bool TemporaryPhysicalControllerIsolation::activate(
    const HidDeviceInfo& apexInterface,
    std::string_view sessionToken,
    std::string& error) {
    if (impl_->active) return true;

    RecoverySnapshot stale{};
    bool staleExists = false;
    if (!readRecoverySnapshot(stale, staleExists, error)) return false;
    if (staleExists) {
        if (processIsRunning(stale.ownerProcessId)) {
            error = "Another ApexSenseBridge process owns the active controller isolation.";
            return false;
        }
        bool recovered = false;
        if (!recoverPendingImpl(recovered, error)) return false;
        impl_->recovered = recovered;
    }

    ScopedHandle device;
    if (!openHidHide(device, error)) return false;
    RecoverySnapshot snapshot{};
    snapshot.ownerProcessId = GetCurrentProcessId();
    if (!getBoolean(device.get(), kIoctlGetActive, snapshot.originalActive,
                    "active state", error) ||
        !getBoolean(device.get(), kIoctlGetInverse, snapshot.originalInverse,
                    "inverse state", error) ||
        !getList(device.get(), kIoctlGetWhitelist, snapshot.originalWhitelist,
                 "application list", error) ||
        !getList(device.get(), kIoctlGetBlacklist, snapshot.originalBlacklist,
                 "device list", error)) {
        return false;
    }
    if (snapshot.originalActive || snapshot.originalInverse) {
        error = "HidHide already has a user-managed active or inverse configuration. "
                "ApexSenseBridge will not overwrite it.";
        return false;
    }

    std::vector<std::wstring> apexPaths;
    if (!apexGameDevicePaths(apexInterface, apexPaths, error)) return false;
    const auto executable = moduleFileName(error);
    if (executable.empty()) return false;
    const auto ntExecutable = imageNtPath(executable, error);
    if (ntExecutable.empty()) return false;

    auto temporaryWhitelist = snapshot.originalWhitelist;
    if (!containsCaseInsensitive(temporaryWhitelist, ntExecutable)) {
        temporaryWhitelist.push_back(ntExecutable);
    }
    // Flydigi's service turns the auxiliary buttons configured in Space
    // Station into virtual keyboard/mouse shortcuts. It must retain read
    // access to the selected physical APEX while HidHide keeps that controller
    // unavailable to the game. The virtual shortcut devices are distinct and
    // remain visible, so this does not reintroduce physical gamepad input.
    for (const auto& service : flydigiSpaceStationServiceNtPaths()) {
        if (!containsCaseInsensitive(temporaryWhitelist, service)) {
            temporaryWhitelist.push_back(service);
        }
    }
    auto temporaryBlacklist = snapshot.originalBlacklist;
    for (const auto& path : apexPaths) {
        if (!containsCaseInsensitive(temporaryBlacklist, path)) {
            temporaryBlacklist.push_back(path);
        }
    }

    if (!writeRecoverySnapshot(snapshot, error)) return false;
    if (!registerRunOnce(executable, error) ||
        !startWatchdog(executable, snapshot.ownerProcessId, sessionToken, error) ||
        !setRecoveryPhase(kPhaseConfigurationMayHaveChanged, error)) {
        clearRecoveryRegistration();
        return false;
    }

    if (!setList(device.get(), kIoctlSetWhitelist, temporaryWhitelist,
                 "application list", error) ||
        !setList(device.get(), kIoctlSetBlacklist, temporaryBlacklist,
                 "device list", error) ||
        !setBoolean(device.get(), kIoctlSetActive, true, "active state", error)) {
        const auto activationError = error;
        bool recovered = false;
        std::string recoveryError;
        if (!recoverPendingImpl(recovered, recoveryError)) {
            error = activationError + "; automatic rollback also failed: " + recoveryError;
        } else {
            error = activationError;
        }
        return false;
    }

    impl_->active = true;
    return true;
}

bool TemporaryPhysicalControllerIsolation::restore(std::string& error) noexcept {
    if (!impl_ || (!impl_->active && !recoveryMarkerExists())) return true;
    bool recovered = false;
    if (!recoverPending(recovered, error)) return false;
    impl_->active = false;
    return true;
}

bool TemporaryPhysicalControllerIsolation::active() const noexcept {
    return impl_ && impl_->active;
}

bool TemporaryPhysicalControllerIsolation::recoveredStaleIsolation() const noexcept {
    return impl_ && impl_->recovered;
}

bool TemporaryPhysicalControllerIsolation::recoverPending(
    bool& recovered, std::string& error) noexcept {
    try {
        return recoverPendingImpl(recovered, error);
    } catch (...) {
        recovered = false;
        error = "Unexpected failure while restoring physical controller visibility.";
        return false;
    }
}

int TemporaryPhysicalControllerIsolation::watchAndRecover(
    std::uint32_t ownerProcessId,
    std::string_view sessionToken,
    std::string& error) noexcept {
    ScopedHandle playniteStopEvent;
    if (!sessionToken.empty()) {
        if (!isValidSessionToken(sessionToken)) {
            error = "The recovery watchdog received an invalid Playnite session token.";
            return 1;
        }
        const auto stopNameAscii = sessionStopEventName(sessionToken);
        const std::wstring stopName(stopNameAscii.begin(), stopNameAscii.end());
        playniteStopEvent.reset(OpenEventW(SYNCHRONIZE, FALSE, stopName.c_str()));
        if (!playniteStopEvent.valid()) {
            error = windowsError(
                "Opening the Playnite stop event from the recovery watchdog",
                GetLastError());
            // Keep the controller hidden. RunOnce/manual recovery remains
            // available, but restoring here could expose it to a running game.
            return 1;
        }
    }

    ScopedHandle owner(OpenProcess(SYNCHRONIZE, FALSE, ownerProcessId));
    if (!owner.valid()) {
        const auto code = GetLastError();
        if (code != ERROR_INVALID_PARAMETER) {
            error = windowsError("Opening the bridge process from the recovery watchdog", code);
            return 1;
        }
    } else if (WaitForSingleObject(owner.get(), INFINITE) != WAIT_OBJECT_0) {
        error = windowsError("Waiting for the bridge process", GetLastError());
        return 1;
    }

    // An unexpected engine exit must remain fail-closed while its Playnite
    // game session is active. Normal shutdown has already signalled this event,
    // so it passes through without adding latency.
    if (playniteStopEvent.valid() &&
        WaitForSingleObject(playniteStopEvent.get(), INFINITE) != WAIT_OBJECT_0) {
        error = windowsError("Waiting for the Playnite game to stop", GetLastError());
        return 1;
    }

    bool recovered = false;
    return recoverPending(recovered, error) ? 0 : 2;
}

namespace detail {

bool matchesFlydigiSpaceStationInstall(
    std::wstring_view displayName,
    std::wstring_view publisher) noexcept {
    // Permit the version/company suffixes used by the official installer,
    // but reject unrelated products whose names merely share these prefixes.
    return hasPrefixBoundary(displayName, kSpaceStationDisplayName) &&
           hasPrefixBoundary(publisher, kSpaceStationPublisher);
}

} // namespace detail

} // namespace asb::platform

#endif // _WIN32
