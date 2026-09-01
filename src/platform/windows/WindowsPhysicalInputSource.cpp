#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>

#include "platform/PhysicalInputSource.h"

#include "platform/HidTransport.h"
#include "platform/XInputGamepad.h"
#include "platform/XInputMapping.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace asb::platform {
namespace {

constexpr USAGE kUsagePageGenericDesktop = 0x01;
constexpr USAGE kUsagePageButton = 0x09;
constexpr USAGE kUsageX = 0x30;
constexpr USAGE kUsageY = 0x31;
constexpr USAGE kUsageZ = 0x32;
constexpr USAGE kUsageRx = 0x33;
constexpr USAGE kUsageRy = 0x34;
constexpr USAGE kUsageRz = 0x35;
constexpr USAGE kUsageHatSwitch = 0x39;
constexpr USAGE kUsageDpadUp = 0x90;
constexpr USAGE kUsageDpadDown = 0x91;
constexpr USAGE kUsageDpadRight = 0x92;
constexpr USAGE kUsageDpadLeft = 0x93;

std::string win32Error(DWORD code) {
    LPSTR buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string message = size && buffer ? std::string(buffer, size)
                                         : "Unknown Win32 error";
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
        message.pop_back();
    }
    return message;
}

bool containsUsage(const HIDP_VALUE_CAPS& cap, USAGE usage) noexcept {
    if (cap.IsRange) {
        return usage >= cap.Range.UsageMin && usage <= cap.Range.UsageMax;
    }
    return usage == cap.NotRange.Usage;
}

bool containsUsage(const HIDP_BUTTON_CAPS& cap, USAGE usage) noexcept {
    if (cap.IsRange) {
        return usage >= cap.Range.UsageMin && usage <= cap.Range.UsageMax;
    }
    return usage == cap.NotRange.Usage;
}

std::int64_t signedLogicalValue(ULONG raw, const HIDP_VALUE_CAPS& cap) noexcept {
    if (cap.LogicalMin >= 0 || cap.BitSize == 0 || cap.BitSize >= 32) {
        return cap.LogicalMin < 0 ? static_cast<std::int32_t>(raw)
                                  : static_cast<std::int64_t>(raw);
    }
    const ULONG signBit = ULONG{1} << (cap.BitSize - 1);
    const ULONG mask = (ULONG{1} << cap.BitSize) - 1;
    raw &= mask;
    return (raw & signBit) != 0
        ? static_cast<std::int64_t>(raw | ~mask)
        : static_cast<std::int64_t>(raw);
}

std::uint8_t normalizeByte(ULONG raw, const HIDP_VALUE_CAPS& cap) noexcept {
    const auto minimum = static_cast<std::int64_t>(cap.LogicalMin);
    const auto maximum = static_cast<std::int64_t>(cap.LogicalMax);
    if (maximum <= minimum) return 0;
    const auto value = std::clamp(signedLogicalValue(raw, cap), minimum, maximum);
    const auto numerator = static_cast<std::uint64_t>(value - minimum) * 255ULL;
    const auto denominator = static_cast<std::uint64_t>(maximum - minimum);
    return static_cast<std::uint8_t>((numerator + denominator / 2) / denominator);
}

DWORD waitMilliseconds(std::chrono::milliseconds timeout) noexcept {
    if (timeout.count() <= 0) return 0;
    constexpr auto maximum =
        static_cast<long long>((std::numeric_limits<DWORD>::max)() - 1);
    return static_cast<DWORD>((std::min)(timeout.count(), maximum));
}

class HidPhysicalInputSource final : public PhysicalInputSource {
public:
    static std::unique_ptr<HidPhysicalInputSource> open(
        const HidDeviceInfo& vendorInterface, std::string& error) {
        std::string enumerateError;
        auto devices = enumerateHidDevices(enumerateError);
        std::vector<HidDeviceInfo> candidates;
        for (auto& device : devices) {
            if (device.vendorId != vendorInterface.vendorId ||
                device.productId != vendorInterface.productId ||
                device.usagePage != kUsagePageGenericDesktop ||
                device.usage != 0x05 || device.inputReportLength == 0) {
                continue;
            }
            if (!vendorInterface.containerId.empty() &&
                device.containerId != vendorInterface.containerId) {
                continue;
            }
            candidates.push_back(std::move(device));
        }
        if (candidates.empty() && !vendorInterface.containerId.empty()) {
            error = "No APEX game-controller HID collection belongs to the selected "
                    "FORCEADAPT interface container.";
            return {};
        }
        if (candidates.empty()) {
            error = enumerateError.empty()
                ? "No APEX game-controller HID collection was found."
                : "APEX game-controller HID enumeration failed: " + enumerateError;
            return {};
        }
        if (candidates.size() != 1) {
            error = "The selected APEX could not be associated with exactly one HID "
                    "game-controller collection.";
            return {};
        }

        HANDLE handle = CreateFileW(
            candidates.front().path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const auto code = GetLastError();
            error = "Could not open the selected APEX HID game-controller collection (" +
                    std::to_string(code) + ": " + win32Error(code) + ").";
            return {};
        }

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (!HidD_GetPreparsedData(handle, &preparsed)) {
            const auto code = GetLastError();
            CloseHandle(handle);
            error = "Could not read the APEX HID descriptor (" +
                    std::to_string(code) + ": " + win32Error(code) + ").";
            return {};
        }

        HIDP_CAPS caps{};
        if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS ||
            caps.InputReportByteLength == 0) {
            HidD_FreePreparsedData(preparsed);
            CloseHandle(handle);
            error = "The selected APEX HID game-controller descriptor is invalid.";
            return {};
        }

        auto source = std::unique_ptr<HidPhysicalInputSource>(
            new HidPhysicalInputSource(std::move(candidates.front()), handle,
                                       preparsed, caps));
        if (!source->loadCapabilities(error)) return {};
        return source;
    }

    ~HidPhysicalInputSource() override {
        if (handle_ != INVALID_HANDLE_VALUE) {
            if (readPending_) {
                CancelIoEx(handle_, &overlapped_);
                DWORD ignored = 0;
                (void)GetOverlappedResult(handle_, &overlapped_, &ignored, TRUE);
            }
            CloseHandle(handle_);
        }
        if (event_) CloseHandle(event_);
        if (preparsed_) HidD_FreePreparsedData(preparsed_);
    }

    PhysicalInputStatus waitForState(
        dualsense::DualSenseInputState& state,
        std::chrono::milliseconds timeout,
        std::string& error) override {
        error.clear();
        if (!ensureReadPending(error)) return PhysicalInputStatus::Error;

        const DWORD waitResult = WaitForSingleObject(event_, waitMilliseconds(timeout));
        if (waitResult == WAIT_TIMEOUT) {
            ++stats_.timeouts;
            return PhysicalInputStatus::Timeout;
        }
        if (waitResult != WAIT_OBJECT_0) {
            const auto code = GetLastError();
            error = "Waiting for an APEX HID input report failed (" +
                    std::to_string(code) + ": " + win32Error(code) + ").";
            return PhysicalInputStatus::Error;
        }

        DWORD bytesRead = 0;
        readPending_ = false;
        if (!GetOverlappedResult(handle_, &overlapped_, &bytesRead, FALSE)) {
            const auto code = GetLastError();
            if (code == ERROR_DEVICE_NOT_CONNECTED || code == ERROR_INVALID_HANDLE ||
                code == ERROR_OPERATION_ABORTED) {
                error = "The physical APEX HID input source disconnected.";
                return PhysicalInputStatus::Disconnected;
            }
            error = "Completing the APEX HID input report failed (" +
                    std::to_string(code) + ": " + win32Error(code) + ").";
            return PhysicalInputStatus::Error;
        }
        if (bytesRead == 0 || bytesRead > report_.size()) {
            ++stats_.parseFailures;
            error = "The physical APEX returned an empty or oversized HID input report.";
            return PhysicalInputStatus::Error;
        }

        if (!parseReport(bytesRead, state, error)) {
            ++stats_.parseFailures;
            return PhysicalInputStatus::Error;
        }
        ++stats_.reports;
        return PhysicalInputStatus::State;
    }

    std::string_view backendName() const noexcept override { return "apex-hid-event"; }
    bool eventDriven() const noexcept override { return true; }
    PhysicalInputSourceStats stats() const noexcept override { return stats_; }

private:
    HidPhysicalInputSource(HidDeviceInfo info, HANDLE handle,
                           PHIDP_PREPARSED_DATA preparsed, HIDP_CAPS caps)
        : info_(std::move(info)), handle_(handle), preparsed_(preparsed), caps_(caps),
          report_(caps.InputReportByteLength, 0) {
        event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        overlapped_.hEvent = event_;
    }

    bool loadCapabilities(std::string& error) {
        if (!event_) {
            error = "Could not create the APEX HID input event.";
            return false;
        }
        valueCaps_.resize(caps_.NumberInputValueCaps);
        USHORT valueCount = static_cast<USHORT>(valueCaps_.size());
        if (valueCount != 0 && HidP_GetValueCaps(
                HidP_Input, valueCaps_.data(), &valueCount, preparsed_) !=
                HIDP_STATUS_SUCCESS) {
            error = "Could not read the APEX HID axis capabilities.";
            return false;
        }
        valueCaps_.resize(valueCount);

        buttonCaps_.resize(caps_.NumberInputButtonCaps);
        USHORT buttonCount = static_cast<USHORT>(buttonCaps_.size());
        if (buttonCount != 0 && HidP_GetButtonCaps(
                HidP_Input, buttonCaps_.data(), &buttonCount, preparsed_) !=
                HIDP_STATUS_SUCCESS) {
            error = "Could not read the APEX HID button capabilities.";
            return false;
        }
        buttonCaps_.resize(buttonCount);

        const auto hasValue = [this](USAGE usage) {
            return std::any_of(valueCaps_.begin(), valueCaps_.end(),
                               [usage](const auto& cap) {
                                   return cap.UsagePage == kUsagePageGenericDesktop &&
                                          containsUsage(cap, usage);
                               });
        };
        if (!hasValue(kUsageX) || !hasValue(kUsageY) || !hasValue(kUsageRx) ||
            !hasValue(kUsageRy) || !hasValue(kUsageZ) || !hasValue(kUsageRz)) {
            std::ostringstream details;
            details << "The APEX HID descriptor does not expose the complete X/Y/Rx/Ry/Z/Rz "
                       "state required for a lossless DualSense proxy. Available value usages:";
            for (const auto& cap : valueCaps_) {
                details << " page=0x" << std::hex << cap.UsagePage << " usage=0x";
                if (cap.IsRange) {
                    details << cap.Range.UsageMin << "-0x" << cap.Range.UsageMax;
                } else {
                    details << cap.NotRange.Usage;
                }
                details << std::dec;
            }
            error = details.str();
            return false;
        }
        return true;
    }

    bool ensureReadPending(std::string& error) {
        if (readPending_) return true;
        ResetEvent(event_);
        std::fill(report_.begin(), report_.end(), std::uint8_t{0});
        if (ReadFile(handle_, report_.data(), static_cast<DWORD>(report_.size()),
                     nullptr, &overlapped_)) {
            SetEvent(event_);
            readPending_ = true;
            return true;
        }
        const auto code = GetLastError();
        if (code == ERROR_IO_PENDING) {
            readPending_ = true;
            return true;
        }
        error = "Starting the APEX HID input read failed (" +
                std::to_string(code) + ": " + win32Error(code) + ").";
        return false;
    }

    bool usageValue(USAGE usage, ULONG& raw,
                    const HIDP_VALUE_CAPS*& matched) const noexcept {
        for (const auto& cap : valueCaps_) {
            if (cap.UsagePage != kUsagePageGenericDesktop ||
                !containsUsage(cap, usage)) {
                continue;
            }
            const auto status = HidP_GetUsageValue(
                HidP_Input, kUsagePageGenericDesktop, cap.LinkCollection,
                usage, &raw, preparsed_,
                reinterpret_cast<PCHAR>(const_cast<std::uint8_t*>(report_.data())),
                static_cast<ULONG>(report_.size()));
            if (status == HIDP_STATUS_SUCCESS) {
                matched = &cap;
                return true;
            }
        }
        return false;
    }

    bool buttonPressed(USAGE usage) const noexcept {
        for (const auto& cap : buttonCaps_) {
            if (cap.UsagePage != kUsagePageButton || !containsUsage(cap, usage)) {
                continue;
            }
            std::array<USAGE, 64> active{};
            ULONG count = static_cast<ULONG>(active.size());
            const auto status = HidP_GetUsages(
                HidP_Input, kUsagePageButton, cap.LinkCollection,
                active.data(), &count, preparsed_,
                reinterpret_cast<PCHAR>(const_cast<std::uint8_t*>(report_.data())),
                static_cast<ULONG>(report_.size()));
            if (status != HIDP_STATUS_SUCCESS && status != HIDP_STATUS_BUFFER_TOO_SMALL) {
                continue;
            }
            if (std::find(active.begin(), active.begin() +
                          static_cast<std::ptrdiff_t>((std::min)(count, static_cast<ULONG>(active.size()))),
                          usage) != active.begin() +
                          static_cast<std::ptrdiff_t>((std::min)(count, static_cast<ULONG>(active.size())))) {
                return true;
            }
        }
        return false;
    }

    bool genericDesktopButton(USAGE usage) const noexcept {
        ULONG raw = 0;
        const HIDP_VALUE_CAPS* cap = nullptr;
        return usageValue(usage, raw, cap) && raw != 0;
    }

    bool parseReport(DWORD bytesRead, dualsense::DualSenseInputState& state,
                     std::string& error) {
        (void)bytesRead;
        dualsense::DualSenseInputState decoded{};
        const auto readAxis = [this, &error](USAGE usage, std::uint8_t& target) {
            ULONG raw = 0;
            const HIDP_VALUE_CAPS* cap = nullptr;
            if (!usageValue(usage, raw, cap) || !cap) {
                error = "An expected APEX HID axis was missing from an input report.";
                return false;
            }
            target = normalizeByte(raw, *cap);
            return true;
        };
        if (!readAxis(kUsageX, decoded.lx) || !readAxis(kUsageY, decoded.ly) ||
            !readAxis(kUsageRx, decoded.rx) || !readAxis(kUsageRy, decoded.ry) ||
            !readAxis(kUsageZ, decoded.l2) || !readAxis(kUsageRz, decoded.r2)) {
            return false;
        }

        std::uint16_t xinputButtons = 0;
        if (buttonPressed(1)) xinputButtons |= xinputButton::kA;
        if (buttonPressed(2)) xinputButtons |= xinputButton::kB;
        if (buttonPressed(3)) xinputButtons |= xinputButton::kX;
        if (buttonPressed(4)) xinputButtons |= xinputButton::kY;
        if (buttonPressed(5)) xinputButtons |= xinputButton::kLeftShoulder;
        if (buttonPressed(6)) xinputButtons |= xinputButton::kRightShoulder;
        if (buttonPressed(7)) xinputButtons |= xinputButton::kBack;
        if (buttonPressed(8)) xinputButtons |= xinputButton::kStart;
        if (buttonPressed(9)) xinputButtons |= xinputButton::kLeftThumb;
        if (buttonPressed(10)) xinputButtons |= xinputButton::kRightThumb;

        ULONG hat = 0;
        const HIDP_VALUE_CAPS* hatCap = nullptr;
        if (usageValue(kUsageHatSwitch, hat, hatCap) && hatCap) {
            const auto direction = signedLogicalValue(hat, *hatCap) - hatCap->LogicalMin;
            if (direction == 0 || direction == 1 || direction == 7)
                xinputButtons |= xinputButton::kDpadUp;
            if (direction == 3 || direction == 4 || direction == 5)
                xinputButtons |= xinputButton::kDpadDown;
            if (direction == 5 || direction == 6 || direction == 7)
                xinputButtons |= xinputButton::kDpadLeft;
            if (direction == 1 || direction == 2 || direction == 3)
                xinputButtons |= xinputButton::kDpadRight;
        } else {
            if (genericDesktopButton(kUsageDpadUp))
                xinputButtons |= xinputButton::kDpadUp;
            if (genericDesktopButton(kUsageDpadDown))
                xinputButtons |= xinputButton::kDpadDown;
            if (genericDesktopButton(kUsageDpadLeft))
                xinputButtons |= xinputButton::kDpadLeft;
            if (genericDesktopButton(kUsageDpadRight))
                xinputButtons |= xinputButton::kDpadRight;
        }

        mapXInputButtons(xinputButtons, decoded.l2, decoded.r2, decoded);
        if (buttonPressed(11)) decoded.buttons |= dualsense::button::kPs;
        state = decoded;
        return true;
    }

    HidDeviceInfo info_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    PHIDP_PREPARSED_DATA preparsed_ = nullptr;
    HIDP_CAPS caps_{};
    HANDLE event_ = nullptr;
    OVERLAPPED overlapped_{};
    bool readPending_ = false;
    std::vector<std::uint8_t> report_;
    std::vector<HIDP_VALUE_CAPS> valueCaps_;
    std::vector<HIDP_BUTTON_CAPS> buttonCaps_;
    PhysicalInputSourceStats stats_{};
};

class XInputPhysicalInputSource final : public PhysicalInputSource {
public:
    explicit XInputPhysicalInputSource(std::unique_ptr<XInputGamepad> gamepad)
        : gamepad_(std::move(gamepad)) {
        // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION is supported on all Windows
        // builds targeted by the installer. Fall back to a regular waitable
        // timer if a compatibility layer rejects the flag.
        constexpr DWORD kHighResolution = 0x00000002;
        timer_ = CreateWaitableTimerExW(
            nullptr, nullptr, kHighResolution, TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!timer_) {
            timer_ = CreateWaitableTimerExW(
                nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
        }
    }

    ~XInputPhysicalInputSource() override {
        if (timer_) CloseHandle(timer_);
    }

    PhysicalInputStatus waitForState(
        dualsense::DualSenseInputState& state,
        std::chrono::milliseconds timeout,
        std::string& error) override {
        if (timeout.count() > 0) {
            LARGE_INTEGER due{};
            due.QuadPart = -static_cast<LONGLONG>(timeout.count()) * 10000LL;
            if (timer_ && SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
                (void)WaitForSingleObject(timer_, INFINITE);
            } else {
                std::this_thread::sleep_for(timeout);
            }
        }
        if (!gamepad_->poll(state, error)) return PhysicalInputStatus::Disconnected;
        ++stats_.reports;
        return PhysicalInputStatus::State;
    }
    std::string_view backendName() const noexcept override { return "xinput-fallback"; }
    bool eventDriven() const noexcept override { return false; }
    PhysicalInputSourceStats stats() const noexcept override { return stats_; }

private:
    std::unique_ptr<XInputGamepad> gamepad_;
    HANDLE timer_ = nullptr;
    PhysicalInputSourceStats stats_{};
};

} // namespace

std::unique_ptr<PhysicalInputSource> openPhysicalInputSource(
    const HidDeviceInfo& apexVendorInterface,
    std::optional<unsigned int> requestedXInputIndex,
    std::string& error) {
    if (!requestedXInputIndex) {
        std::string hidError;
        auto hid = HidPhysicalInputSource::open(apexVendorInterface, hidError);
        if (hid) {
            error.clear();
            return hid;
        }
        error = "APEX HID input unavailable (" + hidError + "); trying XInput fallback.";
    }

    std::string xinputError;
    auto xinput = openXInputGamepadForDevice(
        apexVendorInterface.vendorId, apexVendorInterface.productId,
        requestedXInputIndex, xinputError);
    if (!xinput) {
        if (!error.empty()) error += ' ';
        error += "XInput fallback unavailable: " + xinputError;
        return {};
    }
    return std::make_unique<XInputPhysicalInputSource>(std::move(xinput));
}

} // namespace asb::platform

#endif // _WIN32
