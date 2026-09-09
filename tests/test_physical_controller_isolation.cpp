#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/PhysicalControllerIsolation.h"

#include <cassert>

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
    return 0;
}
