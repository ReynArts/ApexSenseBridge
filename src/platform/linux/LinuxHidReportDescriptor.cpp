#include "platform/linux/LinuxHidReportDescriptor.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace asb::platform {
namespace {

constexpr std::uint8_t kItemTypeMain = 0;
constexpr std::uint8_t kItemTypeGlobal = 1;
constexpr std::uint8_t kItemTypeLocal = 2;

constexpr std::uint8_t kMainInput = 0x08;
constexpr std::uint8_t kMainOutput = 0x09;
constexpr std::uint8_t kMainFeature = 0x0B;
constexpr std::uint8_t kMainCollection = 0x0A;
constexpr std::uint8_t kMainEndCollection = 0x0C;

constexpr std::uint8_t kGlobalUsagePage = 0x00;
constexpr std::uint8_t kGlobalReportSize = 0x07;
constexpr std::uint8_t kGlobalReportId = 0x08;
constexpr std::uint8_t kGlobalReportCount = 0x09;
constexpr std::uint8_t kGlobalPush = 0x0A;
constexpr std::uint8_t kGlobalPop = 0x0B;

constexpr std::uint8_t kLocalUsage = 0x00;

constexpr std::uint8_t kCollectionApplication = 0x01;
constexpr std::uint8_t kLongItemPrefix = 0xFE;

// A descriptor may number up to 256 reports per kind; bits are accumulated per
// report id because one report can be assembled from several main items.
struct ReportBits {
    std::array<std::uint32_t, 256> bits{};
    bool used = false;

    void add(std::uint8_t reportId, std::uint32_t count) noexcept {
        bits[reportId] += count;
        used = true;
    }

    [[nodiscard]] std::uint16_t byteLength(bool numbered) const noexcept {
        if (!used) {
            return 0;
        }
        std::uint32_t widest = 0;
        for (const auto value : bits) {
            widest = (std::max)(widest, value);
        }
        if (widest == 0) {
            return 0;
        }
        const std::uint32_t bytes = (widest + 7) / 8 + (numbered ? 1 : 0);
        return static_cast<std::uint16_t>((std::min)(bytes, std::uint32_t{0xFFFF}));
    }
};

struct GlobalState {
    std::uint16_t usagePage = 0;
    std::uint32_t reportSize = 0;
    std::uint32_t reportCount = 0;
    std::uint8_t reportId = 0;
};

} // namespace

bool parseHidReportDescriptor(std::span<const std::uint8_t> descriptor,
                              HidReportDescriptorInfo& info) noexcept {
    HidReportDescriptorInfo parsed{};
    GlobalState global{};
    std::array<GlobalState, 8> globalStack{};
    std::size_t globalDepth = 0;

    ReportBits input{};
    ReportBits output{};
    ReportBits feature{};

    bool numberedReports = false;
    bool applicationFound = false;
    std::uint16_t pendingUsage = 0;
    bool pendingUsageValid = false;
    int collectionDepth = 0;

    for (std::size_t index = 0; index < descriptor.size();) {
        const std::uint8_t prefix = descriptor[index++];
        if (prefix == kLongItemPrefix) {
            if (index + 1 >= descriptor.size()) {
                return false;
            }
            const std::uint8_t dataSize = descriptor[index];
            index += 2;
            if (index + dataSize > descriptor.size()) {
                return false;
            }
            index += dataSize;
            continue;
        }

        std::size_t size = prefix & 0x03;
        if (size == 3) {
            size = 4;
        }
        const std::uint8_t type = static_cast<std::uint8_t>((prefix >> 2) & 0x03);
        const std::uint8_t tag = static_cast<std::uint8_t>(prefix >> 4);

        if (index + size > descriptor.size()) {
            return false;
        }
        std::uint32_t data = 0;
        for (std::size_t byte = 0; byte < size; ++byte) {
            data |= static_cast<std::uint32_t>(descriptor[index + byte]) << (8 * byte);
        }
        index += size;

        switch (type) {
        case kItemTypeMain:
            switch (tag) {
            case kMainInput:
                input.add(global.reportId, global.reportSize * global.reportCount);
                break;
            case kMainOutput:
                output.add(global.reportId, global.reportSize * global.reportCount);
                break;
            case kMainFeature:
                feature.add(global.reportId, global.reportSize * global.reportCount);
                break;
            case kMainCollection:
                // Only the first top-level application collection identifies
                // the device, matching how Windows reports HIDP_CAPS.
                if (collectionDepth == 0 && data == kCollectionApplication &&
                    !applicationFound) {
                    parsed.usagePage = global.usagePage;
                    parsed.usage = pendingUsageValid ? pendingUsage : 0;
                    applicationFound = true;
                }
                ++collectionDepth;
                break;
            case kMainEndCollection:
                if (collectionDepth > 0) {
                    --collectionDepth;
                }
                break;
            default:
                break;
            }
            pendingUsageValid = false;
            break;

        case kItemTypeGlobal:
            switch (tag) {
            case kGlobalUsagePage:
                global.usagePage = static_cast<std::uint16_t>(data & 0xFFFF);
                break;
            case kGlobalReportSize:
                global.reportSize = data;
                break;
            case kGlobalReportCount:
                global.reportCount = data;
                break;
            case kGlobalReportId:
                global.reportId = static_cast<std::uint8_t>(data & 0xFF);
                numberedReports = true;
                break;
            case kGlobalPush:
                if (globalDepth >= globalStack.size()) {
                    return false;
                }
                globalStack[globalDepth++] = global;
                break;
            case kGlobalPop:
                if (globalDepth == 0) {
                    return false;
                }
                global = globalStack[--globalDepth];
                break;
            default:
                break;
            }
            break;

        case kItemTypeLocal:
            if (tag == kLocalUsage && !pendingUsageValid) {
                // A 4-byte usage carries its page in the high half.
                pendingUsage = static_cast<std::uint16_t>(data & 0xFFFF);
                pendingUsageValid = true;
            }
            break;

        default:
            break;
        }
    }

    if (!applicationFound && !input.used && !output.used && !feature.used) {
        return false;
    }

    parsed.inputReportLength = input.byteLength(numberedReports);
    parsed.outputReportLength = output.byteLength(numberedReports);
    parsed.featureReportLength = feature.byteLength(numberedReports);
    info = parsed;
    return true;
}

} // namespace asb::platform
