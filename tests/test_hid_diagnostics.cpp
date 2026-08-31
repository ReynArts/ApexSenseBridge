#ifdef NDEBUG
#undef NDEBUG
#endif

#include "diagnostics/HidDiagnostics.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

int main() {
    using asb::HidDeviceInfo;
    using namespace asb::diagnostics;

    HidDeviceInfo flydigiByVid{};
    flydigiByVid.vendorId = 0x37D7;
    flydigiByVid.productId = 0x2501;

    HidDeviceInfo flydigiByPath{};
    flydigiByPath.path = L"\\\\?\\hid#VID_37D7&PID_2501&MI_03";

    HidDeviceInfo apexByProduct{};
    apexByProduct.product = L"Flydigi APEX 5";

    HidDeviceInfo vendorDefined{};
    vendorDefined.usagePage = 0xFF42;

    HidDeviceInfo generic{};
    generic.vendorId = 0x1234;
    generic.product = L"Generic gamepad";
    generic.usagePage = 0x0001;

    assert(isRelevantHidDevice(flydigiByVid));
    assert(isRelevantHidDevice(flydigiByPath));
    assert(isRelevantHidDevice(apexByProduct));
    assert(isRelevantHidDevice(vendorDefined));
    assert(!isRelevantHidDevice(generic));

    const std::vector<HidDeviceInfo> all{
        flydigiByVid, flydigiByPath, apexByProduct, vendorDefined, generic};
    assert(selectHidDevices(all, false).size() == 4);
    assert(selectHidDevices(all, true).size() == 5);

    HidDeviceInfo complete{};
    complete.path = L"\\\\?\\hid#vid_37d7&pid_2501&mi_03";
    complete.vendorId = 0x37D7;
    complete.productId = 0x2501;
    complete.manufacturer = L"Flydigi";
    complete.product = L"APEX \"5\"";
    complete.serial = L"ABC\\123";
    complete.usagePage = 0xFFA0;
    complete.usage = 1;
    complete.inputReportLength = 64;
    complete.outputReportLength = 32;
    complete.featureReportLength = 33;
    complete.instanceId = L"HID\\VID_37D7&PID_2501";
    complete.parentInstanceId = L"USB\\VID_37D7&PID_2501";
    complete.interfaceNumber = L"MI_03";
    complete.hardwareIds = {L"HID\\VID_37D7&PID_2501"};
    complete.compatibleIds = {L"HID_DEVICE_SYSTEM_GAME"};

    const std::vector<HidDeviceInfo> one{complete};
    const std::string text = formatHidDevicesText(one);
    assert(text.find("feature=33") != std::string::npos);
    assert(text.find("MI_03") != std::string::npos);
    assert(text.find("Parent instance ID") != std::string::npos);

    const std::string json = formatHidDevicesJson(one);
    assert(json.find("\"count\": 1") != std::string::npos);
    assert(json.find("\"feature_report_length\": 33") != std::string::npos);
    assert(json.find("APEX \\\"5\\\"") != std::string::npos);
    assert(json.find("ABC\\\\123") != std::string::npos);

    std::cout << "HID diagnostic tests passed\n";
    return 0;
}
