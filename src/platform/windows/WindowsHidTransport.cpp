#ifdef _WIN32

#include "platform/HidTransport.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <algorithm>
#include <array>
#include <memory>
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
        if (WriteFile(handle_, wire.data(), static_cast<DWORD>(wire.size()), &written, nullptr) &&
            written == wire.size()) {
            return true;
        }

        const DWORD writeError = GetLastError();
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

private:
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
        if (!SetupDiGetDeviceInterfaceDetailW(
                deviceSet, &interfaceData, detail, detailSize, nullptr, nullptr)) {
            continue;
        }

        HANDLE handle = CreateFileW(detail->DevicePath, 0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }

        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (!HidD_GetAttributes(handle, &attributes)) {
            CloseHandle(handle);
            continue;
        }

        HidDeviceInfo info{};
        info.path = detail->DevicePath;
        info.vendorId = attributes.VendorID;
        info.productId = attributes.ProductID;
        info.manufacturer = hidString(handle, HidD_GetManufacturerString);
        info.product = hidString(handle, HidD_GetProductString);

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (HidD_GetPreparsedData(handle, &preparsed)) {
            HIDP_CAPS caps{};
            if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
                info.usagePage = caps.UsagePage;
                info.usage = caps.Usage;
                info.inputReportLength = caps.InputReportByteLength;
                info.outputReportLength = caps.OutputReportByteLength;
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
                                nullptr, OPEN_EXISTING, 0, nullptr);
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
