#include "platform/AudioEndpointProtection.h"

#include <cassert>

int main() {
    using asb::platform::detail::matchesVirtualDualSenseAudioIdentity;

    assert(matchesVirtualDualSenseAudioIdentity(
        L"USB\\VID_054C&PID_0CE6&MI_00\\3&253044F8&0&0000", L""));
    assert(matchesVirtualDualSenseAudioIdentity(
        L"usb\\vid_054c&pid_0ce6&mi_00\\virtual", L""));
    assert(matchesVirtualDualSenseAudioIdentity(L"", L"Wireless Controller"));
    assert(matchesVirtualDualSenseAudioIdentity(L"", L"wireless controller"));

    assert(!matchesVirtualDualSenseAudioIdentity(
        L"USB\\VID_054C&PID_0CE6&MI_03\\virtual", L""));
    assert(!matchesVirtualDualSenseAudioIdentity(
        L"USB\\VID_1234&PID_5678&MI_00\\speakers", L"Speakers"));
    return 0;
}
