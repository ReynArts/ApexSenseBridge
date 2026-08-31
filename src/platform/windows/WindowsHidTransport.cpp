#ifdef _WIN32

#include "platform/HidTransport.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <cfgmgr32.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace asb::platform {
namespace {

std::string win32Error(DWORD code) {
    LPSTR buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer), 0, nullptr);

    std::string message = size && buffer ? std::string(buffer, size) : "Unknown Win32 error";
    if (buffer) {
        LocalFree(buffer);
    }
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
        message.pop_back();
    }
    return message;
}

std::wstring hidString(HANDLE handle, BOOLEAN(__stdcall* getter)(HANDLE, PVOID, ULONG)) {
    std::array<wchar_t, 256> buffer{};
    if (getter(handle, buffer.data(), static_cast<ULONG>(buffer.size() * sizeof(wchar_t)))) {
        return buffer.data();
    }
    return {};
}

std::wstring registryString(HDEVINFO deviceSet,
                            SP_DEVINFO_DATA& deviceInfo,
                            DWORD property) {
    DWORD requiredSize = 0;
    DWORD propertyType = 0;
    SetupDiGetDeviceRegistryPropertyW(
        deviceSet, &deviceInfo, property, &propertyType, nullptr, 0, &requiredSize);
    if (requiredSize == 0 || (propertyType != REG_SZ && propertyType != REG_EXPAND_SZ)) {
        return {};
    }

    std::vector<BYTE> buffer(requiredSize + sizeof(wchar_t), 0);
    if (!SetupDiGetDeviceRegistryPropertyW(
            deviceSet, &deviceInfo, property, &propertyType,
            buffer.data(), static_cast<DWORD>(buffer.size()), nullptr)) {
        return {};
    }
    return reinterpret_cast<const wchar_t*>(buffer.data());
}

std::vector<std::wstring> registryMultiString(HDEVINFO deviceSet,
                                               SP_DEVINFO_DATA& deviceInfo,
                                               DWORD property) {
    DWORD requiredSize = 0;
    DWORD propertyType = 0;
    SetupDiGetDeviceRegistryPropertyW(
        deviceSet, &deviceInfo, property, &propertyType, nullptr, 0, &requiredSize);
    if (requiredSize == 0 || propertyType != REG_MULTI_SZ) {
        return {};
    }

    std::vector<BYTE> buffer(requiredSize + 2 * sizeof(wchar_t), 0);
    if (!SetupDiGetDeviceRegistryPropertyW(
            deviceSet, &deviceInfo, property, &propertyType,
            buffer.data(), static_cast<DWORD>(buffer.size()), nullptr)) {
        return {};
    }

    std::vector<std::wstring> values;
    const auto* current = reinterpret_cast<const wchar_t*>(buffer.data());
    while (*current != L'\0') {
        values.emplace_back(current);
        current += values.back().size() + 1;
    }
    return values;
}

std::wstring instanceId(HDEVINFO deviceSet, SP_DEVINFO_DATA& deviceInfo) {
    DWORD requiredCharacters = 0;
    SetupDiGetDeviceInstanceIdW(deviceSet, &deviceInfo, nullptr, 0, &requiredCharacters);
    if (requiredCharacters == 0) {
        return {};
    }

    std::vector<wchar_t> buffer(requiredCharacters + 1, L'\0');
    if (!SetupDiGetDeviceInstanceIdW(
            deviceSet, &deviceInfo, buffer.data(),
            static_cast<DWORD>(buffer.size()), nullptr)) {
        return {};
    }
    return buffer.data();
}

std::wstring parentInstanceId(const SP_DEVINFO_DATA& deviceInfo) {
    DEVINST parent = 0;
    if (CM_Get_Parent(&parent, deviceInfo.DevInst, 0) != CR_SUCCESS) {
        return {};
    }

    ULONG requiredCharacters = 0;
    if (CM_Get_Device_ID_Size(&requiredCharacters, parent, 0) != CR_SUCCESS) {
        return {};
    }

    std::vector<wchar_t> buffer(requiredCharacters + 1, L'\0');
    if (CM_Get_Device_IDW(parent, buffer.data(), static_cast<ULONG>(buffer.size()), 0) != CR_SUCCESS) {
        return {};
    }
    return buffer.data();
}

std::wstring interfaceNumber(std::wstring_view path) {
    std::wstring lowered(path);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });

    const auto position = lowered.find(L"mi_");
    if (position == std::wstring::npos || position + 5 > lowered.size() ||
        !std::iswxdigit(lowered[position + 3]) || !std::iswxdigit(lowered[position + 4])) {
        return {};
    }

    std::wstring result = L"MI_";
    result.push_back(static_cast<wchar_t>(std::towupper(lowered[position + 3])));
    result.push_back(static_cast<wchar_t>(std::towupper(lowered[position + 4])));
    return result;
}

std::optional<std::uint16_t> idFromPath(std::wstring_view path, std::wstring_view marker) {
    std::wstring lowered(path);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });

    const auto position = lowered.find(marker);
    if (position == std::wstring::npos || position + marker.size() + 4 > lowered.size()) {
        return std::nullopt;
    }

    std::uint16_t result = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        const wchar_t ch = lowered[position + marker.size() + index];
        result = static_cast<std::uint16_t>(result << 4);
        if (ch >= L'0' && ch <= L'9') {
            result = static_cast<std::uint16_t>(result + ch - L'0');
        } else if (ch >= L'a' && ch <= L'f') {
            result = static_cast<std::uint16_t>(result + ch - L'a' + 10);
        } else {
            return std::nullopt;
        }
    }
    return result;
}

class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~ScopedHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

DWORD waitMilliseconds(std::chrono::milliseconds timeout) noexcept {
    if (timeout.count() <= 0) {
        return 0;
    }
    constexpr auto maximum = static_cast<long long>(std::numeric_limits<DWORD>::max() - 1);
    return static_cast<DWORD>(std::min(timeout.count(), maximum));
}

class WindowsHidTransport final : public HidTransport {
public:
    WindowsHidTransport(HidDeviceInfo info, HANDLE handle)
        : info_(std::move(info)), handle_(handle) {}

    ~WindowsHidTransport() override {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    bool isOpen() const noexcept override {
        return handle_ != INVALID_HANDLE_VALUE;
    }

    const HidDeviceInfo& info() const noexcept override {
        return info_;
    }

    bool writeOutputReport(std::span<const std::uint8_t> report, std::string& error) override {
        if (!isOpen()) {
            error = "HID handle is not open";
            return false;
        }
        if (info_.outputReportLength == 0) {
            error = "This HID interface declares no output report";
            return false;
        }
        if (report.size() > info_.outputReportLength) {
            std::ostringstream oss;
            oss << "Protocol report is " << report.size()
                << " bytes, but HID output report length is only " << info_.outputReportLength;
            error = oss.str();
            return false;
        }

        std::vector<std::uint8_t> wire(info_.outputReportLength, 0);
        std::copy(report.begin(), report.end(), wire.begin());

        DWORD written = 0;
        DWORD writeError = ERROR_SUCCESS;
        if (writeFileOverlapped(wire, written, writeError) && written == wire.size()) {
            return true;
        }

        if (HidD_SetOutputReport(handle_, wire.data(), static_cast<ULONG>(wire.size()))) {
            return true;
        }

        const DWORD hidError = GetLastError();
        std::ostringstream oss;
        oss << "WriteFile failed (" << writeError << ": " << win32Error(writeError)
            << "); HidD_SetOutputReport also failed (" << hidError << ": "
            << win32Error(hidError) << ")";
        error = oss.str();
        return false;
    }

    HidReadStatus readInputReport(std::span<std::uint8_t> report,
                                  std::chrono::milliseconds timeout,
                                  std::size_t& bytesRead,
                                  std::string& error) override {
        bytesRead = 0;
        if (!isOpen()) {
            error = "HID handle is not open";
            return HidReadStatus::Error;
        }
        if (report.empty()) {
            error = "HID input buffer is empty";
            return HidReadStatus::Error;
        }

        ScopedHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.valid()) {
            const auto code = GetLastError();
            error = "CreateEventW for HID read failed (" + std::to_string(code) +
                    ": " + win32Error(code) + ')';
            return HidReadStatus::Error;
        }

        OVERLAPPED overlapped{};
        overlapped.hEvent = event.get();
        if (!ReadFile(handle_, report.data(), static_cast<DWORD>(report.size()),
                      nullptr, &overlapped)) {
            const auto readError = GetLastError();
            if (readError != ERROR_IO_PENDING) {
                error = "ReadFile failed (" + std::to_string(readError) +
                        ": " + win32Error(readError) + ')';
                return HidReadStatus::Error;
            }
        }

        const DWORD waitResult = WaitForSingleObject(event.get(), waitMilliseconds(timeout));
        if (waitResult == WAIT_TIMEOUT) {
            CancelIoEx(handle_, &overlapped);
            DWORD completed = 0;
            if (GetOverlappedResult(handle_, &overlapped, &completed, TRUE)) {
                bytesRead = completed;
                return completed > 0 ? HidReadStatus::Data : HidReadStatus::Timeout;
            }
            const auto completionError = GetLastError();
            if (completionError == ERROR_OPERATION_ABORTED) {
                return HidReadStatus::Timeout;
            }
            error = "HID read cancellation failed (" + std::to_string(completionError) +
                    ": " + win32Error(completionError) + ')';
            return HidReadStatus::Error;
        }
        if (waitResult != WAIT_OBJECT_0) {
            const auto waitError = GetLastError();
            CancelIoEx(handle_, &overlapped);
            DWORD ignored = 0;
            GetOverlappedResult(handle_, &overlapped, &ignored, TRUE);
            error = "Waiting for HID input failed (" + std::to_string(waitError) +
                    ": " + win32Error(waitError) + ')';
            return HidReadStatus::Error;
        }

        DWORD completed = 0;
        if (!GetOverlappedResult(handle_, &overlapped, &completed, FALSE)) {
            const auto completionError = GetLastError();
            error = "Completing HID read failed (" + std::to_string(completionError) +
                    ": " + win32Error(completionError) + ')';
            return HidReadStatus::Error;
        }
        bytesRead = completed;
        return completed > 0 ? HidReadStatus::Data : HidReadStatus::Timeout;
    }

private:
    bool writeFileOverlapped(std::span<const std::uint8_t> wire,
                             DWORD& written,
                             DWORD& errorCode) {
        written = 0;
        ScopedHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.valid()) {
            errorCode = GetLastError();
            return false;
        }

        OVERLAPPED overlapped{};
        overlapped.hEvent = event.get();
        if (!WriteFile(handle_, wire.data(), static_cast<DWORD>(wire.size()),
                       nullptr, &overlapped)) {
            errorCode = GetLastError();
            if (errorCode != ERROR_IO_PENDING) {
                return false;
            }
        }

        constexpr auto writeTimeout = std::chrono::milliseconds(1000);
        const DWORD waitResult = WaitForSingleObject(event.get(), waitMilliseconds(writeTimeout));
        if (waitResult != WAIT_OBJECT_0) {
            errorCode = waitResult == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
            CancelIoEx(handle_, &overlapped);
            DWORD ignored = 0;
            GetOverlappedResult(handle_, &overlapped, &ignored, TRUE);
            return false;
        }
        if (!GetOverlappedResult(handle_, &overlapped, &written, FALSE)) {
            errorCode = GetLastError();
            return false;
        }
        errorCode = written == wire.size() ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
        return written == wire.size();
    }

    HidDeviceInfo info_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

} // namespace

std::vector<HidDeviceInfo> enumerateHidDevices(std::string& error) {
    std::vector<HidDeviceInfo> devices;

    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO deviceSet = SetupDiGetClassDevsW(
        &hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceSet == INVALID_HANDLE_VALUE) {
        error = "SetupDiGetClassDevsW failed: " + win32Error(GetLastError());
        return devices;
    }

    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(deviceSet, nullptr, &hidGuid, index, &interfaceData)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) {
                error = "SetupDiEnumDeviceInterfaces failed: " + win32Error(GetLastError());
            }
            break;
        }

        DWORD detailSize = 0;
        SetupDiGetDeviceInterfaceDetailW(deviceSet, &interfaceData, nullptr, 0, &detailSize, nullptr);
        if (detailSize == 0) {
            continue;
        }

        std::vector<std::byte> detailBuffer(detailSize);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA deviceInfo{};
        deviceInfo.cbSize = sizeof(deviceInfo);
        if (!SetupDiGetDeviceInterfaceDetailW(
                deviceSet, &interfaceData, detail, detailSize, nullptr, &deviceInfo)) {
            continue;
        }

        HidDeviceInfo info{};
        info.path = detail->DevicePath;
        info.friendlyName = registryString(deviceSet, deviceInfo, SPDRP_FRIENDLYNAME);
        info.className = registryString(deviceSet, deviceInfo, SPDRP_CLASS);
        info.hardwareIds = registryMultiString(deviceSet, deviceInfo, SPDRP_HARDWAREID);
        info.compatibleIds = registryMultiString(deviceSet, deviceInfo, SPDRP_COMPATIBLEIDS);
        info.instanceId = instanceId(deviceSet, deviceInfo);
        info.parentInstanceId = parentInstanceId(deviceInfo);
        info.interfaceNumber = interfaceNumber(info.path);

        if (const auto vendorId = idFromPath(info.path, L"vid_")) {
            info.vendorId = *vendorId;
        }
        if (const auto productId = idFromPath(info.path, L"pid_")) {
            info.productId = *productId;
        }

        HANDLE handle = CreateFileW(detail->DevicePath, 0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            devices.push_back(std::move(info));
            continue;
        }

        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (!HidD_GetAttributes(handle, &attributes)) {
            CloseHandle(handle);
            devices.push_back(std::move(info));
            continue;
        }

        info.vendorId = attributes.VendorID;
        info.productId = attributes.ProductID;
        info.manufacturer = hidString(handle, HidD_GetManufacturerString);
        info.product = hidString(handle, HidD_GetProductString);
        info.serial = hidString(handle, HidD_GetSerialNumberString);

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (HidD_GetPreparsedData(handle, &preparsed)) {
            HIDP_CAPS caps{};
            if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
                info.usagePage = caps.UsagePage;
                info.usage = caps.Usage;
                info.inputReportLength = caps.InputReportByteLength;
                info.outputReportLength = caps.OutputReportByteLength;
                info.featureReportLength = caps.FeatureReportByteLength;
            }
            HidD_FreePreparsedData(preparsed);
        }

        devices.push_back(std::move(info));
        CloseHandle(handle);
    }

    SetupDiDestroyDeviceInfoList(deviceSet);
    return devices;
}

HidTransport* createHidTransport(const HidDeviceInfo& info, std::string& error) {
    HANDLE handle = CreateFileW(info.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto code = GetLastError();
        std::ostringstream oss;
        oss << "Could not open Flydigi vendor HID interface (" << code << ": "
            << win32Error(code) << ")";
        error = oss.str();
        return nullptr;
    }

    return new WindowsHidTransport(info, handle);
}

void destroyHidTransport(HidTransport* transport) noexcept {
    delete transport;
}

} // namespace asb::platform

#endif // _WIN32
