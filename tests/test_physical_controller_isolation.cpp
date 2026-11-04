#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/PhysicalControllerIsolation.h"

#include <cassert>
#include <optional>
#include <string>
#include <vector>

int main() {
    using asb::platform::detail::matchesFlydigiSpaceStationInstall;
    using asb::platform::detail::matchesFlydigiVirtualGamepadTopology;
    using asb::platform::detail::matchesFlydigiVirtualGamepadRoot;
    using asb::platform::detail::matchesApexProfileRecoveryDevice;

    assert(matchesFlydigiSpaceStationInstall(
        L"Flydigi Space Station 4.2.2.3", L"Flydigi, Inc."));
    assert(matchesFlydigiSpaceStationInstall(
        L"FLYDIGI SPACE STATION", L"Flydigi Electronics"));

    assert(!matchesFlydigiSpaceStationInstall(
        L"Flydigi Space Station Helper", L"Unknown Publisher"));
    assert(!matchesFlydigiSpaceStationInstall(
        L"Flydigi Space StationEvil", L"Flydigi, Inc."));
    assert(!matchesFlydigiSpaceStationInstall(
        L"Flydigi Space Station 4.2.2.3", L"FlydigiEvil"));
    assert(!matchesFlydigiSpaceStationInstall(
        L"Another Controller Tool", L"Flydigi, Inc."));

    assert(matchesFlydigiVirtualGamepadTopology(
        L"HID\\VID_054C&PID_0CE6&REV_0100\\2&2AF0CFB&0&0000",
        L"USB\\VID_054C&PID_0CE6&REV_0100\\1&2CC2035A&0&01",
        L"ROOT\\GENITECH_VIRTUAL_GAMEPAD_DEVICE\\0000",
        L"hidvirtualdriver"));
    assert(matchesFlydigiVirtualGamepadTopology(
        L"hid\\vid_054c&pid_0ce6\\gamepad",
        L"usb\\vid_054c&pid_0ce6\\container",
        L"root\\genitech_virtual_gamepad_device\\0001",
        L"HIDVIRTUALDRIVER"));

    // Space Station's verified Xbox/XInput proxy must be hidden as well as its
    // DualSense proxy.
    assert(matchesFlydigiVirtualGamepadTopology(
        L"HID\\VID_045E&PID_028E&IG_00\\GAMEPAD",
        L"USB\\VID_045E&PID_028E\\CONTAINER",
        L"ROOT\\GENITECH_VIRTUAL_GAMEPAD_DEVICE\\0002",
        L"hidvirtualdriver"));
    assert(matchesFlydigiVirtualGamepadRoot(
        L"ROOT\\GENITECH_VIRTUAL_GAMEPAD_DEVICE\\0000",
        L"hidvirtualdriver"));
    assert(!matchesFlydigiVirtualGamepadRoot(
        L"ROOT\\GENITECH_VIRTUAL_GAMEPAD_DEVICE\\0000",
        L"hidvirtualdriver_evil"));
    assert(!matchesFlydigiVirtualGamepadRoot(
        L"ROOT\\GENITECH_VIRTUAL_GAMEPAD_DEVICE_EVIL\\0000",
        L"hidvirtualdriver"));

    // ApexSenseBridge/VIIPER, physical controllers, and lookalike root/service
    // identities must all remain visible.
    assert(!matchesFlydigiVirtualGamepadTopology(
        L"HID\\VID_054C&PID_0CE6&MI_03\\4&127B94DB&0&0000",
        L"USB\\VID_054C&PID_0CE6\\2&3B7C36A2&0&1",
        L"ROOT\\USBIP_VHCI\\0000", L"usbip_vhci"));
    assert(!matchesFlydigiVirtualGamepadTopology(
        L"HID\\VID_054C&PID_0CE6\\REAL",
        L"USB\\VID_054C&PID_0CE6\\REAL",
        L"USB\\ROOT_HUB30\\4&1234&0&0", L"USBHUB3"));
    assert(!matchesFlydigiVirtualGamepadTopology(
        L"HID\\VID_054C&PID_0CE6\\GAMEPAD",
        L"USB\\VID_054C&PID_0CE6\\CONTAINER",
        L"ROOT\\GENITECH_VIRTUAL_GAMEPAD_DEVICE_EVIL\\0000",
        L"hidvirtualdriver"));
    assert(!matchesFlydigiVirtualGamepadTopology(
        L"HID\\VID_054C&PID_0CE6\\GAMEPAD",
        L"USB\\VID_054C&PID_0CE6\\CONTAINER",
        L"ROOT\\GENITECH_VIRTUAL_GAMEPAD_DEVICE\\0000",
        L"hidvirtualdriver_evil"));
    assert(matchesFlydigiVirtualGamepadTopology(
        L"HID\\VID_054C&PID_0DF2\\GAMEPAD",
        L"USB\\VID_054C&PID_0DF2\\CONTAINER",
        L"ROOT\\GENITECH_VIRTUAL_GAMEPAD_DEVICE\\0000",
        L"hidvirtualdriver"));
    assert(!matchesFlydigiVirtualGamepadTopology(
        L"USB\\VID_045E&PID_028E\\GAMEPAD",
        L"USB\\VID_045E&PID_028E\\CONTAINER",
        L"ROOT\\GENITECH_VIRTUAL_GAMEPAD_DEVICE\\0000",
        L"hidvirtualdriver"));
    assert(!matchesFlydigiVirtualGamepadTopology(
        L"HID\\VID_045E&PID_028E\\GAMEPAD", L"",
        L"ROOT\\GENITECH_VIRTUAL_GAMEPAD_DEVICE\\0000",
        L"hidvirtualdriver"));

    asb::HidDeviceInfo apex{};
    apex.path = L"\\\\?\\hid#vid_37d7&pid_2501&mi_00#vendor";
    apex.containerId = L"{11223344-5566-7788-99AA-BBCCDDEEFF00}";
    apex.vendorId = 0x37D7;
    apex.productId = 0x2501;
    apex.usagePage = 0xFF00;
    assert(matchesApexProfileRecoveryDevice(
        apex, L"\\\\?\\HID#VID_37D7&PID_2501&MI_00#VENDOR", L"{OTHER}",
        0x37D7, 0x2501, 0xFF00));
    assert(matchesApexProfileRecoveryDevice(
        apex, L"\\\\?\\hid#old-path", L"{11223344-5566-7788-99aa-bbccddeeff00}",
        0x37D7, 0x2501, 0xFF00));

    apex.productId = 0x2502;
    assert(!matchesApexProfileRecoveryDevice(
        apex, apex.path, apex.containerId, 0x37D7, 0x2501, 0xFF00));
    apex.productId = 0x2501;
    apex.containerId = L"{UNRELATED}";
    assert(!matchesApexProfileRecoveryDevice(
        apex, L"\\\\?\\hid#old-path", L"{11223344-5566-7788-99AA-BBCCDDEEFF00}",
        0x37D7, 0x2501, 0xFF00));

    using asb::platform::detail::ApexRestoreTarget;
    using asb::platform::detail::ApexSettingsRestore;
    using asb::platform::detail::deferredApexRestoreBlocksSession;
    using asb::platform::detail::kDeferredApexRestoreRegistryKey;
    using asb::platform::detail::kRecoveryRegistryKey;
    using asb::platform::detail::locateApexRestoreTarget;
    using asb::platform::detail::mergeDeferredApexRestore;

    asb::HidDeviceInfo receiver{};
    receiver.path = L"\\\\?\\hid#vid_37d7&pid_2501&mi_02#receiver";
    receiver.containerId = L"{AAAAAAAA-0000-0000-0000-000000000001}";
    receiver.vendorId = 0x37D7;
    receiver.productId = 0x2501;
    receiver.usagePage = 0xFFA0;
    asb::HidDeviceInfo cable = receiver;
    cable.path = L"\\\\?\\hid#vid_37d7&pid_2503&mi_02#cable";
    cable.containerId = L"{BBBBBBBB-0000-0000-0000-000000000002}";
    cable.productId = 0x2503;
    asb::HidDeviceInfo apex6 = receiver;
    apex6.path = L"\\\\?\\hid#vid_37d7&pid_2502&mi_02#apex6";
    apex6.containerId = L"{CCCCCCCC-0000-0000-0000-000000000003}";
    apex6.productId = 0x2502;

    ApexSettingsRestore legacy{};
    legacy.vendorPath = receiver.path;
    legacy.containerId = receiver.containerId;
    legacy.vendorId = receiver.vendorId;
    legacy.productId = receiver.productId;
    legacy.usagePage = receiver.usagePage;
    legacy.profilePending = true;
    legacy.profileSlot = 2;
    legacy.inputTransportPending = true;
    legacy.controllerData = false;
    legacy.rawData = true;

    assert(locateApexRestoreTarget(legacy, {}, false) == ApexRestoreTarget::Absent);
    assert(locateApexRestoreTarget(legacy, {cable, apex6}, false) ==
           ApexRestoreTarget::Absent);
    assert(locateApexRestoreTarget(legacy, {}, true) == ApexRestoreTarget::Unknown);
    assert(locateApexRestoreTarget(legacy, {apex6, receiver}, false) ==
           ApexRestoreTarget::Present);
    assert(locateApexRestoreTarget(legacy, {receiver}, true) ==
           ApexRestoreTarget::Present);
    asb::HidDeviceInfo replugged = receiver;
    replugged.path = L"\\\\?\\hid#vid_37d7&pid_2501&mi_02#new-instance";
    assert(locateApexRestoreTarget(legacy, {replugged}, false) ==
           ApexRestoreTarget::Present);

    std::optional<ApexSettingsRestore> deferred;
    assert(mergeDeferredApexRestore(deferred, legacy));
    assert(deferred && deferred->vendorPath == legacy.vendorPath &&
           deferred->profilePending && deferred->profileSlot == 2 &&
           deferred->inputTransportPending && !deferred->controllerData &&
           deferred->rawData);

    assert(!deferredApexRestoreBlocksSession(*deferred, apex6));
    assert(!deferredApexRestoreBlocksSession(*deferred, cable));
    assert(deferredApexRestoreBlocksSession(*deferred, receiver));
    assert(deferredApexRestoreBlocksSession(*deferred, replugged));

    ApexSettingsRestore later = legacy;
    later.profileSlot = 3;
    later.controllerData = true;
    later.rawData = false;
    assert(mergeDeferredApexRestore(deferred, later));
    assert(deferred->profileSlot == 2 && !deferred->controllerData &&
           deferred->rawData);

    ApexSettingsRestore otherConnection = later;
    otherConnection.vendorPath = cable.path;
    otherConnection.containerId = cable.containerId;
    otherConnection.productId = cable.productId;
    assert(!mergeDeferredApexRestore(deferred, otherConnection));
    assert(deferred->vendorPath == legacy.vendorPath && deferred->profileSlot == 2);

    ApexSettingsRestore profileOnly = legacy;
    profileOnly.inputTransportPending = false;
    std::optional<ApexSettingsRestore> partial = profileOnly;
    ApexSettingsRestore transportOnly = legacy;
    transportOnly.profilePending = false;
    transportOnly.controllerData = true;
    transportOnly.rawData = true;
    assert(mergeDeferredApexRestore(partial, transportOnly));
    assert(partial->profilePending && partial->profileSlot == 2 &&
           partial->inputTransportPending && partial->controllerData &&
           partial->rawData);

    using asb::platform::detail::ApexRecoveryMode;
    using asb::platform::detail::ApexRestoreOutcome;
    using asb::platform::detail::defersApexRestore;
    using asb::platform::detail::recoveryMarkerUnchanged;
    using asb::platform::detail::remainingDeferredApexRestore;

    const auto stale = ApexRecoveryMode::StaleSession;
    const auto own = ApexRecoveryMode::OwnSession;
    const auto activation = ApexRecoveryMode::SessionActivation;
    assert(defersApexRestore(stale, ApexRestoreOutcome::Absent));
    assert(defersApexRestore(stale, ApexRestoreOutcome::Unresponsive));
    assert(!defersApexRestore(stale, ApexRestoreOutcome::Failed));
    assert(!defersApexRestore(stale, ApexRestoreOutcome::Restored));
    assert(!defersApexRestore(own, ApexRestoreOutcome::Absent));
    assert(!defersApexRestore(own, ApexRestoreOutcome::Unresponsive));
    const auto ownAbsent = ApexRecoveryMode::OwnSessionControllerAbsent;
    assert(defersApexRestore(ownAbsent, ApexRestoreOutcome::Absent));
    assert(defersApexRestore(ownAbsent, ApexRestoreOutcome::Unresponsive));
    assert(!defersApexRestore(ownAbsent, ApexRestoreOutcome::Failed));
    assert(!defersApexRestore(ownAbsent, ApexRestoreOutcome::Restored));
    assert(defersApexRestore(activation, ApexRestoreOutcome::NotAttempted));
    assert(!defersApexRestore(activation, ApexRestoreOutcome::Restored));

    ApexSettingsRestore asleep = legacy;
    asleep.inputTransportPending = false;
    assert(locateApexRestoreTarget(asleep, {receiver, apex6}, false) ==
           ApexRestoreTarget::Present);
    std::optional<ApexSettingsRestore> unanswered;
    assert(defersApexRestore(stale, ApexRestoreOutcome::Unresponsive) &&
           mergeDeferredApexRestore(unanswered, asleep));
    assert(unanswered && unanswered->profilePending && unanswered->profileSlot == 2 &&
           !unanswered->inputTransportPending);
    assert(!deferredApexRestoreBlocksSession(*unanswered, apex6));
    assert(!deferredApexRestoreBlocksSession(*unanswered, cable));
    assert(deferredApexRestoreBlocksSession(*unanswered, receiver));
    assert(deferredApexRestoreBlocksSession(*unanswered, replugged));

    assert(!remainingDeferredApexRestore(legacy, true, true));
    const auto transportLeft = remainingDeferredApexRestore(legacy, true, false);
    assert(transportLeft && !transportLeft->profilePending &&
           transportLeft->inputTransportPending && !transportLeft->controllerData &&
           transportLeft->rawData && transportLeft->vendorPath == legacy.vendorPath);
    const auto nothingRestored = remainingDeferredApexRestore(legacy, false, false);
    assert(nothingRestored && nothingRestored->profilePending &&
           nothingRestored->profileSlot == 2 && nothingRestored->inputTransportPending);
    assert(!remainingDeferredApexRestore(asleep, true, false));

    assert(recoveryMarkerUnchanged(4242u, 4242u));
    assert(!recoveryMarkerUnchanged(4243u, 4242u));
    assert(!recoveryMarkerUnchanged(std::nullopt, 4242u));

    const std::wstring recoveryTree = std::wstring(kRecoveryRegistryKey) + L"\\";
    assert(kDeferredApexRestoreRegistryKey != kRecoveryRegistryKey);
    assert(kDeferredApexRestoreRegistryKey.substr(0, recoveryTree.size()) !=
           recoveryTree);

    // Issue #28: DSX keeps HidHide's single-client control device open.
    using asb::platform::detail::describeHidHideControlDenied;
    using asb::platform::detail::findHidHideControlClients;

    assert(findHidHideControlClients({L"explorer.exe", L"steam.exe"}).empty());
    assert(findHidHideControlClients({}).empty());
    const auto clients = findHidHideControlClients(
        {L"hidhideclient.EXE", L"steam.exe", L"dsx.exe", L"DSX.exe"});
    assert((clients == std::vector<std::string>{"DSX.exe", "HidHideClient.exe"}));
    // Lookalike executables must not be blamed.
    assert(findHidHideControlClients({L"DSXHelper.exe", L"MyDSX.exe"}).empty());

    using asb::platform::detail::kHidHideBusyMarker;
    using asb::platform::detail::nextHidHideOpenDelay;
    using std::chrono::milliseconds;

    const auto named = describeHidHideControlDenied({"DSX.exe"});
    assert(named.rfind(kHidHideBusyMarker, 0) == 0);
    assert(named.find("DSX (DSX.exe) is holding the HidHide control device") !=
           std::string::npos);
    assert(named.find("Close it or disable its HidHide integration") !=
           std::string::npos);
    assert(named.find("restart Windows") == std::string::npos);
    const auto several = describeHidHideControlDenied(
        {"DSX.exe", "HidHideClient.exe"});
    assert(several.find("DSX (DSX.exe), HidHide Configuration Client "
                        "(HidHideClient.exe) are holding") != std::string::npos);
    const auto generic = describeHidHideControlDenied({});
    assert(generic.rfind(kHidHideBusyMarker, 0) == 0);
    assert(generic.find("restart Windows") != std::string::npos);
    assert(generic.find("DSX") != std::string::npos);

    // Open retry backoff: 100, 200, 400, then 500 ms, bounded by the window.
    const milliseconds window{10000};
    assert(nextHidHideOpenDelay(1, milliseconds(0), window) == milliseconds(100));
    assert(nextHidHideOpenDelay(2, milliseconds(100), window) == milliseconds(200));
    assert(nextHidHideOpenDelay(3, milliseconds(300), window) == milliseconds(400));
    assert(nextHidHideOpenDelay(4, milliseconds(700), window) == milliseconds(500));
    assert(nextHidHideOpenDelay(40, milliseconds(5000), window) == milliseconds(500));
    assert(nextHidHideOpenDelay(40, milliseconds(9800), window) == milliseconds(200));
    assert(nextHidHideOpenDelay(40, window, window) == milliseconds(0));
    assert(nextHidHideOpenDelay(1, milliseconds(0), milliseconds(0)) == milliseconds(0));

    using asb::platform::detail::mergeMissingIsolationTargets;
    using Targets = std::vector<std::wstring>;

    const Targets hidden{
        L"HID\\VID_37D7&PID_2501&IG_00\\7&1A2B3C4D&0&0000",
        L"USB\\VID_37D7&PID_2501&MI_00\\6&5E6F7A8B&0&0000",
        L"ROOT\\GENITECH_VIRTUAL_GAMEPAD_DEVICE\\0000"};
    assert(!mergeMissingIsolationTargets(hidden, hidden));
    assert(!mergeMissingIsolationTargets(
        hidden, {L"usb\\vid_37d7&pid_2501&mi_00\\6&5e6f7a8b&0&0000"}));

    const auto oneMissing = mergeMissingIsolationTargets(
        hidden, {hidden[0], L"HID\\VID_37D7&PID_2503&IG_00\\8&CAFE&0&0000"});
    assert(oneMissing);
    assert((*oneMissing == Targets{
        hidden[0], hidden[1], hidden[2],
        L"HID\\VID_37D7&PID_2503&IG_00\\8&CAFE&0&0000"}));

    const auto caseDuplicates = mergeMissingIsolationTargets(
        hidden,
        {L"USB\\VID_37D7&PID_2503&MI_00\\9&BEEF&0&0000",
         L"usb\\vid_37d7&pid_2503&mi_00\\9&beef&0&0000",
         L"rOOt\\genitech_virtual_gamepad_device\\0000"});
    assert(caseDuplicates);
    assert((*caseDuplicates == Targets{
        hidden[0], hidden[1], hidden[2],
        L"USB\\VID_37D7&PID_2503&MI_00\\9&BEEF&0&0000"}));

    const Targets required{L"HID\\NEW\\A", L"hid\\new\\a", L"", L"USB\\NEW\\B"};
    const auto fromEmpty = mergeMissingIsolationTargets({}, required);
    assert(fromEmpty);
    assert((*fromEmpty == Targets{L"HID\\NEW\\A", L"USB\\NEW\\B"}));

    assert(!mergeMissingIsolationTargets(hidden, {}));
    assert(!mergeMissingIsolationTargets({}, {}));
    assert(!mergeMissingIsolationTargets(hidden, {L""}));
    return 0;
}
