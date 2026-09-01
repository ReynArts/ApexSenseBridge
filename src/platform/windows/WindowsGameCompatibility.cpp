#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include "platform/GameCompatibility.h"

#include <array>
#include <filesystem>
#include <string>

namespace asb::platform {
namespace {

constexpr wchar_t kInputKey[] =
    L"Software\\Insomniac Games\\Marvel's Spider-Man 2\\Input";
constexpr wchar_t kValueName[] = L"EnableWindowsGamingInput";
constexpr wchar_t kRunOnceKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
constexpr wchar_t kRunOnceValue[] = L"!ApexSenseBridgeRestoreSpiderMan2Wgi";

std::string windowsError(DWORD code) {
    return "Windows error " + std::to_string(code);
}

bool spiderMan2Running() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"Spider-Man2.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

std::filesystem::path markerPath() {
    std::array<wchar_t, 32768> localAppData{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData.data(), static_cast<DWORD>(localAppData.size()));
    if (length == 0 || length >= localAppData.size()) return {};
    return std::filesystem::path(localAppData.data()) /
           L"ApexSenseBridge" / L"spiderman2-wgi-restore.txt";
}

bool setRegistryValue(bool existed, DWORD value, std::string& error) {
    HKEY key = nullptr;
    const auto openStatus = RegOpenKeyExW(
        HKEY_CURRENT_USER, kInputKey, 0, KEY_SET_VALUE, &key);
    if (openStatus != ERROR_SUCCESS) {
        error = "Could not open Spider-Man 2 input settings: " + windowsError(openStatus);
        return false;
    }
    LSTATUS status = ERROR_SUCCESS;
    if (existed) {
        status = RegSetValueExW(key, kValueName, 0, REG_DWORD,
                                reinterpret_cast<const BYTE*>(&value), sizeof(value));
    } else {
        status = RegDeleteValueW(key, kValueName);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        error = "Could not restore Spider-Man 2 Windows Gaming Input: " + windowsError(status);
        return false;
    }
    return true;
}

bool writeMarker(const std::filesystem::path& path, bool existed, DWORD value,
                 std::string& error) {
    std::error_code filesystemError;
    std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError) {
        error = "Could not create the compatibility backup directory.";
        return false;
    }
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "Could not create the temporary WGI restore marker: " +
                windowsError(GetLastError());
        return false;
    }
    const std::string data = std::string(existed ? "1 " : "0 ") + std::to_string(value);
    DWORD written = 0;
    const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                    written == data.size();
    const auto code = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok) {
        error = "Could not write the temporary WGI restore marker: " + windowsError(code);
        return false;
    }
    return true;
}

void removeRunOnce() noexcept {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunOnceKey, 0, KEY_SET_VALUE, &key) ==
        ERROR_SUCCESS) {
        RegDeleteValueW(key, kRunOnceValue);
        RegCloseKey(key);
    }
}

bool registerRunOnce(std::string& error) {
    std::array<wchar_t, 32768> executable{};
    const DWORD length = GetModuleFileNameW(
        nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
        error = "Could not resolve ApexSenseBridge for WGI crash recovery.";
        return false;
    }
    HKEY key = nullptr;
    const auto openStatus = RegCreateKeyExW(
        HKEY_CURRENT_USER, kRunOnceKey, 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &key, nullptr);
    if (openStatus != ERROR_SUCCESS) {
        error = "Could not register WGI recovery at next login: " +
                windowsError(openStatus);
        return false;
    }
    const std::wstring command =
        L"\"" + std::wstring(executable.data(), length) +
        L"\" restore-controller-visibility";
    const auto setStatus = RegSetValueExW(
        key, kRunOnceValue, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (setStatus != ERROR_SUCCESS) {
        error = "Could not register WGI recovery at next login: " +
                windowsError(setStatus);
        return false;
    }
    return true;
}

bool readMarker(const std::filesystem::path& path, bool& existed, DWORD& value) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::array<char, 64> data{};
    DWORD read = 0;
    const bool ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size() - 1), &read, nullptr) != FALSE;
    CloseHandle(file);
    if (!ok || read < 3) return false;
    data[read] = '\0';
    existed = data[0] == '1';
    try {
        value = static_cast<DWORD>(std::stoul(data.data() + 2));
    } catch (...) {
        return false;
    }
    return true;
}

} // namespace

struct TemporarySpiderMan2WgiOverride::Impl {
    bool active = false;
    bool originalExisted = false;
    DWORD originalValue = 0;
    bool recovered = false;
    std::filesystem::path marker = markerPath();
};

TemporarySpiderMan2WgiOverride::TemporarySpiderMan2WgiOverride()
    : impl_(std::make_unique<Impl>()) {}

TemporarySpiderMan2WgiOverride::~TemporarySpiderMan2WgiOverride() {
    std::string ignored;
    (void)restore(ignored);
}

bool TemporarySpiderMan2WgiOverride::activate(std::string& error) {
    if (impl_->active) return true;
    if (impl_->marker.empty()) {
        error = "LOCALAPPDATA is unavailable; refusing a registry change without a recovery marker.";
        return false;
    }

    std::error_code markerError;
    const bool markerExists = std::filesystem::exists(impl_->marker, markerError);
    if (markerExists) {
        bool recovered = false;
        if (!recoverPending(recovered, error)) return false;
        impl_->recovered = recovered;
    }
    if (spiderMan2Running()) {
        error = "Spider-Man 2 is already running; close it before enabling the temporary WGI override.";
        return false;
    }

    HKEY key = nullptr;
    const auto openStatus = RegOpenKeyExW(
        HKEY_CURRENT_USER, kInputKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key);
    if (openStatus != ERROR_SUCCESS) {
        error = "Could not open Spider-Man 2 input settings: " + windowsError(openStatus);
        return false;
    }
    DWORD type = 0;
    DWORD size = sizeof(impl_->originalValue);
    const auto queryStatus = RegQueryValueExW(
        key, kValueName, nullptr, &type,
        reinterpret_cast<BYTE*>(&impl_->originalValue), &size);
    impl_->originalExisted = queryStatus == ERROR_SUCCESS;
    if (queryStatus != ERROR_SUCCESS && queryStatus != ERROR_FILE_NOT_FOUND) {
        RegCloseKey(key);
        error = "Could not read Spider-Man 2 Windows Gaming Input: " + windowsError(queryStatus);
        return false;
    }
    if (impl_->originalExisted && (type != REG_DWORD || size != sizeof(DWORD))) {
        RegCloseKey(key);
        error = "Spider-Man 2 Windows Gaming Input has an unexpected registry type.";
        return false;
    }
    if (!writeMarker(impl_->marker, impl_->originalExisted, impl_->originalValue, error)) {
        RegCloseKey(key);
        return false;
    }
    if (!registerRunOnce(error)) {
        RegCloseKey(key);
        std::error_code ignored;
        std::filesystem::remove(impl_->marker, ignored);
        return false;
    }
    const DWORD disabled = 0;
    const auto setStatus = RegSetValueExW(
        key, kValueName, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&disabled), sizeof(disabled));
    RegCloseKey(key);
    if (setStatus != ERROR_SUCCESS) {
        std::error_code ignored;
        std::filesystem::remove(impl_->marker, ignored);
        removeRunOnce();
        error = "Could not temporarily disable Windows Gaming Input: " + windowsError(setStatus);
        return false;
    }
    impl_->active = true;
    return true;
}

bool TemporarySpiderMan2WgiOverride::restore(std::string& error) noexcept {
    if (!impl_ || !impl_->active) return true;
    if (!setRegistryValue(impl_->originalExisted, impl_->originalValue, error)) return false;
    impl_->active = false;
    std::error_code ignored;
    std::filesystem::remove(impl_->marker, ignored);
    removeRunOnce();
    return true;
}

bool TemporarySpiderMan2WgiOverride::active() const noexcept {
    return impl_ && impl_->active;
}

bool TemporarySpiderMan2WgiOverride::recoveredStaleOverride() const noexcept {
    return impl_ && impl_->recovered;
}

bool TemporarySpiderMan2WgiOverride::recoverPending(
    bool& recovered, std::string& error) noexcept {
    recovered = false;
    try {
        const auto marker = markerPath();
        if (marker.empty()) {
            error = "LOCALAPPDATA is unavailable; the WGI recovery marker cannot be located.";
            return false;
        }
        std::error_code filesystemError;
        if (!std::filesystem::exists(marker, filesystemError)) return true;
        bool existed = false;
        DWORD value = 0;
        if (!readMarker(marker, existed, value)) {
            error = "A WGI recovery marker exists but is unreadable.";
            return false;
        }
        if (!setRegistryValue(existed, value, error)) return false;
        std::filesystem::remove(marker, filesystemError);
        if (filesystemError) {
            error = "Spider-Man 2 WGI was restored, but its recovery marker could not be removed.";
            return false;
        }
        removeRunOnce();
        recovered = true;
        return true;
    } catch (...) {
        error = "Unexpected failure while restoring Spider-Man 2 Windows Gaming Input.";
        return false;
    }
}

} // namespace asb::platform
