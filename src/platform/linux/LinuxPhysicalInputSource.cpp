#ifdef __linux__

#include "platform/PhysicalInputSource.h"

#include "flydigi/Apex4Protocol.h"
#include "flydigi/Apex5Protocol.h"
#include "platform/VendorPhysicalInputSource.h"

#include <utility>

namespace asb::platform {

// Mirrors the dispatch order of the Windows implementation: the vendor
// interface first for each protocol generation, because it carries the
// complete controller state on the interface the bridge already owns. Linux
// has no XInput, and the HID game-controller collection fallback is only
// needed if a future pad stops publishing vendor input.
std::unique_ptr<PhysicalInputSource> openPhysicalInputSource(
    const HidDeviceInfo& apexVendorInterface,
    std::optional<unsigned int> requestedXInputIndex,
    std::string& error) {
    if (requestedXInputIndex.has_value()) {
        error = "--xinput-index is a Windows-only escape hatch. On Linux the "
                "bridge reads the APEX vendor interface directly.";
        return {};
    }

    std::string combinedError;

    if (apexVendorInterface.vendorId == flydigi::kVendorId &&
        flydigi::isControllerProduct(apexVendorInterface.productId)) {
        std::string apex5Error;
        auto apex5 = openApex5VendorInputSource(apexVendorInterface, apex5Error);
        if (apex5) {
            error.clear();
            return apex5;
        }
        combinedError = "Apex 5 V2 vendor input unavailable (" + apex5Error + "); ";
    }

    if (flydigi::isApex4Product(apexVendorInterface.vendorId,
                                apexVendorInterface.productId)) {
        std::string apex4Error;
        auto apex4 = openApex4VendorInputSource(apexVendorInterface, apex4Error);
        if (apex4) {
            error.clear();
            return apex4;
        }
        combinedError += "Apex 4 V1 input unavailable (" + apex4Error + "); ";
    }

    error = combinedError +
            "no usable APEX input stream was found on the selected interface.";
    return {};
}

} // namespace asb::platform

#endif // __linux__
