#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/CompetingHapticsEngines.h"

#include <cassert>
#include <string>
#include <vector>

int main() {
    using namespace asb::platform;

    assert(findCompetingHapticsEngines({}).empty());
    assert(findCompetingHapticsEngines({L"explorer.exe", L"Steam.exe", L"SpaceStationService.exe"}).empty());
    assert(findCompetingHapticsEngines({L"XHaptics.exe.bak", L"XHaptics", L"my-XHaptics.exe"}).empty());

    const auto launcher = findCompetingHapticsEngines({L"explorer.exe", L"xhaptics.EXE"});
    assert(launcher == std::vector<std::string>{"XHaptics.exe"});

    const auto installed = findCompetingHapticsEngines({L"explorer.exe", L"xHaptics Engine.exe"});
    assert(installed == std::vector<std::string>{"xHaptics Engine.exe"});
    assert(findCompetingHapticsEngines({L"XHAPTICS ENGINE.EXE"}) == installed);
    assert(findCompetingHapticsEngines({L"xHaptics.exe", L"xHaptics Engine.exe.bak"}) ==
           std::vector<std::string>{"XHaptics.exe"});

    const auto both = findCompetingHapticsEngines(
        {L"X-HAPTIC-ENGINE-FUSION.EXE", L"XHaptics.exe", L"XHaptics.exe"});
    assert((both == std::vector<std::string>{"XHaptics.exe", "x-haptic-engine-fusion.exe"}));

    assert(competingHapticsEnginesWarning({}).empty());
    const auto warning = competingHapticsEnginesWarning(both);
    assert(warning.rfind("WARNING: ", 0) == 0);
    assert(warning.find("XHaptics.exe, x-haptic-engine-fusion.exe") != std::string::npos);
    assert(warning.find('\n') == std::string::npos);
    return 0;
}
