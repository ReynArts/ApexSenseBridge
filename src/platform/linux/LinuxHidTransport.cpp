#ifdef __linux__

#include "platform/HidTransport.h"
#include "platform/linux/LinuxHidReportDescriptor.h"
#include "platform/linux/LinuxText.h"

#include <hidapi/hidapi.h>
#include <libudev.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace asb::platform {
namespace {

using linux_text::widen;

struct UdevDeleter {
    void operator()(struct udev* handle) const noexcept { udev_unref(handle); }
};
struct UdevEnumerateDeleter {
    void operator()(struct udev_enumerate* handle) const noexcept {
        udev_enumerate_unref(handle);
    }
};
struct UdevDeviceDeleter {
    void operator()(struct udev_device* handle) const noexcept {
        udev_device_unref(handle);
    }
};

std::string_view safeString(const char* value) noexcept {
    return value ? std::string_view(value) : std::string_view{};
}

// hidapi initialises lazily, but doing it once up front keeps the first
// enumeration from racing several transports opening at the same time.
bool ensureHidApi(std::string& error) {
    static std::once_flag once;
    static bool initialised = false;
    std::call_once(once, [] { initialised = hid_init() == 0; });
    if (!initialised) {
        error = "hid_init() failed; the hidraw backend is unavailable.";
    }
    return initialised;
}

// HID_ID has the shape "0003:0000054C:00000CE6" (bus:vendor:product).
bool parseHidId(std::string_view value,
                std::uint16_t& vendorId,
                std::uint16_t& productId) noexcept {
    unsigned bus = 0;
    unsigned vendor = 0;
    unsigned product = 0;
    if (value.empty()) {
        return false;
    }
    const std::string text(value);
    if (std::sscanf(text.c_str(), "%x:%x:%x", &bus, &vendor, &product) != 3) {
        return false;
    }
    vendorId = static_cast<std::uint16_t>(vendor & 0xFFFF);
    productId = static_cast<std::uint16_t>(product & 0xFFFF);
    return true;
}

std::vector<std::uint8_t> readReportDescriptor(std::string_view hidSyspath) {
    std::string path(hidSyspath);
    path += "/report_descriptor";
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

class HidrawTransport final : public HidTransport {
public:
    HidrawTransport(hid_device* device, HidDeviceInfo info)
        : device_(device), info_(std::move(info)) {}

    ~HidrawTransport() override {
        if (device_) {
            hid_close(device_);
        }
    }

    [[nodiscard]] bool isOpen() const noexcept override { return device_ != nullptr; }
    [[nodiscard]] const HidDeviceInfo& info() const noexcept override { return info_; }

    bool writeOutputReport(std::span<const std::uint8_t> report,
                           std::string& error) override {
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
                << " bytes, but HID output report length is only "
                << info_.outputReportLength;
            error = oss.str();
            return false;
        }

        // Pad to the declared length exactly as the Windows transport does, so
        // the vendor protocol sees an identical wire format on both platforms.
        std::vector<std::uint8_t> wire(info_.outputReportLength, 0);
        std::copy(report.begin(), report.end(), wire.begin());

        const int written = hid_write(device_, wire.data(), wire.size());
        if (written == static_cast<int>(wire.size())) {
            return true;
        }
        error = "hid_write failed: " + lastHidError();
        return false;
    }

    bool readFeatureReport(std::span<std::uint8_t> report, std::string& error) override {
        if (!isOpen()) {
            error = "HID handle is not open";
            return false;
        }
        if (report.empty()) {
            error = "HID feature-report buffer has an invalid size";
            return false;
        }
        // report[0] carries the requested report ID, matching HidD_GetFeature.
        const int read = hid_get_feature_report(device_, report.data(), report.size());
        if (read > 0) {
            return true;
        }
        error = "hid_get_feature_report failed: " + lastHidError();
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
            error = "HID input-report buffer has an invalid size";
            return HidReadStatus::Error;
        }

        const int read = hid_read_timeout(device_, report.data(), report.size(),
                                          static_cast<int>(timeout.count()));
        if (read > 0) {
            bytesRead = static_cast<std::size_t>(read);
            return HidReadStatus::Data;
        }
        if (read == 0) {
            return HidReadStatus::Timeout;
        }
        error = "hid_read_timeout failed: " + lastHidError();
        return HidReadStatus::Error;
    }

private:
    std::string lastHidError() const {
        const wchar_t* message = hid_error(device_);
        if (!message) {
            return "no detail reported";
        }
        return linux_text::narrow(message);
    }

    hid_device* device_ = nullptr;
    HidDeviceInfo info_{};
};

} // namespace

std::vector<HidDeviceInfo> enumerateHidDevices(std::string& error) {
    std::vector<HidDeviceInfo> devices;

    const std::unique_ptr<struct udev, UdevDeleter> context(udev_new());
    if (!context) {
        error = "udev_new() failed; HID enumeration is unavailable.";
        return devices;
    }

    const std::unique_ptr<struct udev_enumerate, UdevEnumerateDeleter> enumerate(
        udev_enumerate_new(context.get()));
    if (!enumerate) {
        error = "udev_enumerate_new() failed.";
        return devices;
    }
    udev_enumerate_add_match_subsystem(enumerate.get(), "hidraw");
    udev_enumerate_scan_devices(enumerate.get());

    struct udev_list_entry* entry = nullptr;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate.get())) {
        const char* syspath = udev_list_entry_get_name(entry);
        if (!syspath) {
            continue;
        }
        const std::unique_ptr<struct udev_device, UdevDeviceDeleter> node(
            udev_device_new_from_syspath(context.get(), syspath));
        if (!node) {
            continue;
        }
        const char* devnode = udev_device_get_devnode(node.get());
        if (!devnode) {
            continue;
        }

        HidDeviceInfo info{};
        info.path = widen(devnode);
        info.instanceId = widen(safeString(udev_device_get_syspath(node.get())));
        info.className = L"hidraw";

        // The immediate parent is the HID device that owns this node; it holds
        // the identity properties and the report descriptor.
        struct udev_device* hid = udev_device_get_parent(node.get());
        if (hid) {
            const auto hidSyspath = safeString(udev_device_get_syspath(hid));
            parseHidId(safeString(udev_device_get_property_value(hid, "HID_ID")),
                       info.vendorId, info.productId);
            info.product = widen(safeString(udev_device_get_property_value(hid, "HID_NAME")));
            info.friendlyName = info.product;
            info.serial = widen(safeString(udev_device_get_property_value(hid, "HID_UNIQ")));
            info.hardwareIds.push_back(
                widen(safeString(udev_device_get_property_value(hid, "MODALIAS"))));
            info.compatibleIds.push_back(
                widen(safeString(udev_device_get_property_value(hid, "HID_ID"))));

            HidReportDescriptorInfo descriptor{};
            if (parseHidReportDescriptor(readReportDescriptor(hidSyspath), descriptor)) {
                info.usagePage = descriptor.usagePage;
                info.usage = descriptor.usage;
                info.inputReportLength = descriptor.inputReportLength;
                info.outputReportLength = descriptor.outputReportLength;
                info.featureReportLength = descriptor.featureReportLength;
            }
        }

        if (struct udev_device* usbInterface = udev_device_get_parent_with_subsystem_devtype(
                node.get(), "usb", "usb_interface")) {
            info.parentInstanceId =
                widen(safeString(udev_device_get_syspath(usbInterface)));
        }

        // containerId groups every interface of one physical device, which is
        // what the Windows ContainerId is used for. Bluetooth and virtual
        // devices have no usb_device ancestor, so they fall back to their own
        // HID node and simply form a container of one.
        if (struct udev_device* usbDevice = udev_device_get_parent_with_subsystem_devtype(
                node.get(), "usb", "usb_device")) {
            info.containerId = widen(safeString(udev_device_get_syspath(usbDevice)));
            info.manufacturer =
                widen(safeString(udev_device_get_sysattr_value(usbDevice, "manufacturer")));
            const auto product = safeString(udev_device_get_sysattr_value(usbDevice, "product"));
            if (!product.empty()) {
                info.product = widen(product);
            }
            const auto serial = safeString(udev_device_get_sysattr_value(usbDevice, "serial"));
            if (!serial.empty()) {
                info.serial = widen(serial);
            }
        } else if (hid) {
            info.containerId = widen(safeString(udev_device_get_syspath(hid)));
        }

        // interfaceNumber stays empty on purpose: the MI_xx matchers in the
        // shared selection code accept an empty value, and Linux has no
        // equivalent spelling to offer them.
        devices.push_back(std::move(info));
    }

    return devices;
}

HidTransport* createHidTransport(const HidDeviceInfo& info, std::string& error) {
    if (!ensureHidApi(error)) {
        return nullptr;
    }

    const auto path = linux_text::narrow(info.path);
    hid_device* device = hid_open_path(path.c_str());
    if (!device) {
        error = "Could not open " + path +
                ". Install the ApexSenseBridge udev rule, or check that no other "
                "process holds the interface exclusively.";
        return nullptr;
    }
    return new HidrawTransport(device, info);
}

void destroyHidTransport(HidTransport* transport) noexcept {
    delete transport;
}

} // namespace asb::platform

#endif // __linux__
