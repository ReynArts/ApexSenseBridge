#include "diagnostics/HidDiagnostics.h"

#include "flydigi/Apex5Protocol.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string_view>

namespace asb::diagnostics {
namespace {

std::wstring lower(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t ch) {
        if (ch >= L'A' && ch <= L'Z') {
            return static_cast<wchar_t>(ch - L'A' + L'a');
        }
        return ch;
    });
    return result;
}

bool contains(std::wstring_view haystack, std::wstring_view needle) {
    return lower(haystack).find(lower(needle)) != std::wstring::npos;
}

void appendUtf8CodePoint(std::string& output, std::uint32_t value) {
    if (value <= 0x7F) {
        output.push_back(static_cast<char>(value));
    } else if (value <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (value >> 6)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    } else if (value <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (value >> 12)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (value >> 18)));
        output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    }
}

std::string toUtf8(std::wstring_view value) {
    std::string output;
    output.reserve(value.size());

    for (std::size_t index = 0; index < value.size(); ++index) {
        std::uint32_t codePoint = static_cast<std::uint32_t>(value[index]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (codePoint >= 0xD800 && codePoint <= 0xDBFF && index + 1 < value.size()) {
                const auto low = static_cast<std::uint32_t>(value[index + 1]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                    ++index;
                }
            }
        }
        appendUtf8CodePoint(output, codePoint);
    }
    return output;
}

std::string display(std::wstring_view value) {
    return value.empty() ? "<not available>" : toUtf8(value);
}

std::string hex16(std::uint16_t value) {
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << value;
    return output.str();
}

void appendTextList(std::ostringstream& output,
                    std::string_view label,
                    const std::vector<std::wstring>& values) {
    output << "    " << label << ":";
    if (values.empty()) {
        output << " <not available>\n";
        return;
    }
    output << '\n';
    for (const auto& value : values) {
        output << "      - " << display(value) << '\n';
    }
}

std::string escapeJson(std::wstring_view value) {
    const std::string utf8 = toUtf8(value);
    std::ostringstream output;
    for (const unsigned char ch : utf8) {
        switch (ch) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (ch < 0x20) {
                output << "\\u" << std::hex << std::uppercase
                       << std::setw(4) << std::setfill('0') << static_cast<unsigned int>(ch)
                       << std::dec;
            } else {
                output << static_cast<char>(ch);
            }
        }
    }
    return output.str();
}

void appendJsonString(std::ostringstream& output, std::wstring_view value) {
    output << '"' << escapeJson(value) << '"';
}

void appendJsonStringArray(std::ostringstream& output,
                           const std::vector<std::wstring>& values,
                           std::string_view indent) {
    output << '[';
    if (!values.empty()) {
        output << '\n';
        for (std::size_t index = 0; index < values.size(); ++index) {
            output << indent << "  ";
            appendJsonString(output, values[index]);
            output << (index + 1 == values.size() ? "\n" : ",\n");
        }
        output << indent;
    }
    output << ']';
}

} // namespace

bool isRelevantHidDevice(const HidDeviceInfo& info) {
    const bool vendorUsagePage = info.usagePage >= 0xFF00;
    return info.vendorId == flydigi::kVendorId ||
           contains(info.path, L"vid_37d7") ||
           contains(info.manufacturer, L"flydigi") ||
           contains(info.manufacturer, L"apex") ||
           contains(info.product, L"flydigi") ||
           contains(info.product, L"apex") ||
           vendorUsagePage;
}

std::vector<HidDeviceInfo> selectHidDevices(std::span<const HidDeviceInfo> devices,
                                            bool includeAllHid) {
    std::vector<HidDeviceInfo> selected;
    selected.reserve(devices.size());
    std::copy_if(devices.begin(), devices.end(), std::back_inserter(selected),
                 [includeAllHid](const HidDeviceInfo& info) {
                     return includeAllHid || isRelevantHidDevice(info);
                 });
    return selected;
}

std::string formatHidDevicesText(std::span<const HidDeviceInfo> devices) {
    std::ostringstream output;
    output << "HID diagnostic: " << devices.size() << " interface(s)\n";
    if (devices.empty()) {
        output << "No matching HID interface found.\n";
        return output.str();
    }

    for (std::size_t index = 0; index < devices.size(); ++index) {
        const auto& info = devices[index];
        output << "\n[" << index << "]\n"
               << "    Device path: " << display(info.path) << '\n'
               << "    VID:PID: " << hex16(info.vendorId) << ':' << hex16(info.productId) << '\n'
               << "    Manufacturer: " << display(info.manufacturer) << '\n'
               << "    Product: " << display(info.product) << '\n'
               << "    Serial: " << display(info.serial) << '\n'
               << "    Friendly name: " << display(info.friendlyName) << '\n'
               << "    Class: " << display(info.className) << '\n'
               << "    Usage page / usage: " << hex16(info.usagePage)
               << " / " << hex16(info.usage) << '\n'
               << "    Report lengths: input=" << info.inputReportLength
               << " output=" << info.outputReportLength
               << " feature=" << info.featureReportLength << '\n'
               << "    Instance ID: " << display(info.instanceId) << '\n'
               << "    Parent instance ID: " << display(info.parentInstanceId) << '\n'
               << "    Interface number: " << display(info.interfaceNumber) << '\n';
        appendTextList(output, "Hardware IDs", info.hardwareIds);
        appendTextList(output, "Compatible IDs", info.compatibleIds);
    }
    return output.str();
}

std::string formatHidDevicesJson(std::span<const HidDeviceInfo> devices) {
    std::ostringstream output;
    output << "{\n  \"count\": " << devices.size() << ",\n  \"devices\": [";
    if (!devices.empty()) {
        output << '\n';
    }

    for (std::size_t index = 0; index < devices.size(); ++index) {
        const auto& info = devices[index];
        output << "    {\n"
               << "      \"index\": " << index << ",\n"
               << "      \"device_path\": "; appendJsonString(output, info.path); output << ",\n"
               << "      \"vendor_id\": " << info.vendorId << ",\n"
               << "      \"vendor_id_hex\": \"" << hex16(info.vendorId) << "\",\n"
               << "      \"product_id\": " << info.productId << ",\n"
               << "      \"product_id_hex\": \"" << hex16(info.productId) << "\",\n"
               << "      \"manufacturer\": "; appendJsonString(output, info.manufacturer); output << ",\n"
               << "      \"product\": "; appendJsonString(output, info.product); output << ",\n"
               << "      \"serial\": "; appendJsonString(output, info.serial); output << ",\n"
               << "      \"friendly_name\": "; appendJsonString(output, info.friendlyName); output << ",\n"
               << "      \"class\": "; appendJsonString(output, info.className); output << ",\n"
               << "      \"usage_page\": " << info.usagePage << ",\n"
               << "      \"usage_page_hex\": \"" << hex16(info.usagePage) << "\",\n"
               << "      \"usage\": " << info.usage << ",\n"
               << "      \"usage_hex\": \"" << hex16(info.usage) << "\",\n"
               << "      \"input_report_length\": " << info.inputReportLength << ",\n"
               << "      \"output_report_length\": " << info.outputReportLength << ",\n"
               << "      \"feature_report_length\": " << info.featureReportLength << ",\n"
               << "      \"instance_id\": "; appendJsonString(output, info.instanceId); output << ",\n"
               << "      \"parent_instance_id\": "; appendJsonString(output, info.parentInstanceId); output << ",\n"
               << "      \"interface_number\": "; appendJsonString(output, info.interfaceNumber); output << ",\n"
               << "      \"hardware_ids\": "; appendJsonStringArray(output, info.hardwareIds, "      "); output << ",\n"
               << "      \"compatible_ids\": "; appendJsonStringArray(output, info.compatibleIds, "      "); output << '\n'
               << "    }" << (index + 1 == devices.size() ? "\n" : ",\n");
    }

    output << "  ]\n}\n";
    return output.str();
}

} // namespace asb::diagnostics
