#ifdef NDEBUG
#undef NDEBUG
#endif

#include "diagnostics/HidDiagnostics.h"

#include <cassert>
#include <cstdint>
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

    const auto apexInterface = [](const wchar_t* path, std::uint16_t productId,
                                  std::uint16_t usagePage) {
        HidDeviceInfo info{};
        info.path = path;
        info.vendorId = productId == 0x2412 ? 0x04B4 : 0x37D7;
        info.productId = productId;
        info.usagePage = usagePage;
        return info;
    };
    const auto dongleA = apexInterface(
        L"\\\\?\\hid#vid_37d7&pid_2401&mi_02&col03#8&1111&0&0002#{4d1e55b2}", 0x2401, 0xFFA0);
    const auto dongleB = apexInterface(
        L"\\\\?\\hid#vid_37d7&pid_2401&mi_02&col03#8&2222&0&0002#{4d1e55b2}", 0x2401, 0xFFA0);
    const auto wiredA = apexInterface(
        L"\\\\?\\hid#vid_37d7&pid_2402&mi_02&col03#8&0aaa&0&0002#{4d1e55b2}", 0x2402, 0xFFA0);
    const auto apex4 = apexInterface(
        L"\\\\?\\hid#vid_04b4&pid_2412&mi_02#7&1a2b&0&0000#{4d1e55b2}", 0x2412, 0xFFA0);
    auto dongleAUpper = dongleA;
    dongleAUpper.path = L"\\\\?\\HID#VID_37D7&PID_2401&MI_02&COL03#8&1111&0&0002#{4D1E55B2}";

    assert(orderApexCandidates({}).empty());
    const auto single = orderApexCandidates({dongleB});
    assert(single.size() == 1 && single[0].path == dongleB.path);

    const auto twoSameType = orderApexCandidates({dongleB, dongleA});
    const auto twoSameTypeReversed = orderApexCandidates({dongleA, dongleB});
    assert(twoSameType.size() == 2 && twoSameType[0].path == dongleA.path &&
           twoSameType[1].path == dongleB.path);
    assert(twoSameTypeReversed[0].path == twoSameType[0].path &&
           twoSameTypeReversed[1].path == twoSameType[1].path);

    const auto mixedTransports = orderApexCandidates({wiredA, dongleB});
    assert(mixedTransports.size() == 2 && mixedTransports[0].path == dongleB.path);

    const auto apex5And4 = orderApexCandidates({dongleA, apex4});
    assert(apex5And4.size() == 2 && apex5And4[0].productId == 0x2412 &&
           apex5And4[1].productId == 0x2401);

    const auto three = orderApexCandidates({wiredA, apex4, dongleB});
    const auto threeShuffled = orderApexCandidates({dongleB, wiredA, apex4});
    assert(three.size() == 3);
    for (std::size_t index = 0; index < three.size(); ++index) {
        assert(three[index].path == threeShuffled[index].path);
    }

    const auto duplicates = orderApexCandidates({dongleA, dongleB, dongleAUpper, dongleA});
    assert(duplicates.size() == 2 && duplicates[0].path == dongleA.path);

    std::vector<std::size_t> verified;
    const auto none = firstVerifiedCandidate(0, [&](std::size_t index) {
        verified.push_back(index);
        return true;
    }, {});
    assert(!none && verified.empty());

    const auto firstAnswers = firstVerifiedCandidate(2, [&](std::size_t index) {
        verified.push_back(index);
        return true;
    }, {});
    assert(firstAnswers && *firstAnswers == 0 && verified == std::vector<std::size_t>{0});

    verified.clear();
    const auto sleepingFirst = firstVerifiedCandidate(3, [&](std::size_t index) {
        verified.push_back(index);
        return index == 1;
    }, {});
    assert(sleepingFirst && *sleepingFirst == 1 &&
           (verified == std::vector<std::size_t>{0, 1}));

    verified.clear();
    const auto nobodyAnswers = firstVerifiedCandidate(3, [&](std::size_t index) {
        verified.push_back(index);
        return false;
    }, {});
    assert(!nobodyAnswers && (verified == std::vector<std::size_t>{0, 1, 2}));

    verified.clear();
    bool stop = false;
    const auto stopped = firstVerifiedCandidate(3, [&](std::size_t index) {
        verified.push_back(index);
        stop = true;
        return false;
    }, [&] { return stop; });
    assert(!stopped && verified == std::vector<std::size_t>{0});

    const std::string multiJson = formatHidDevicesJson(three);
    assert(multiJson.find("\"count\": 3") != std::string::npos);
    assert(multiJson.find("\"index\": 2") != std::string::npos);

    std::cout << "HID diagnostic tests passed\n";
    return 0;
}
