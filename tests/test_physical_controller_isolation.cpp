#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/PhysicalControllerIsolation.h"

#include <cassert>

int main() {
    using asb::platform::detail::matchesFlydigiSpaceStationInstall;

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
    return 0;
}
