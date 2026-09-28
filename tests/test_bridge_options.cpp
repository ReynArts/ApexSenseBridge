#ifdef NDEBUG
#undef NDEBUG
#endif

#include "cli/BridgeOptions.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct ParseResult {
    asb::cli::BridgeCommandOptions options;
    std::string error;
    bool succeeded = false;
};

ParseResult parse(std::initializer_list<std::string_view> arguments) {
    std::vector<std::string> storage;
    storage.reserve(arguments.size() + 2);
    storage.emplace_back("ApexSenseBridge");
    storage.emplace_back("bridge-triggers");
    for (const auto argument : arguments) {
        storage.emplace_back(argument);
    }

    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& argument : storage) {
        argv.push_back(argument.data());
    }

    ParseResult result;
    result.succeeded = asb::cli::parseBridgeOptions(
        static_cast<int>(argv.size()), argv.data(), result.options, result.error);
    return result;
}

} // namespace

int main() {
    using Backend = asb::dualsense::VirtualDualSenseBackend;
    using Profile = asb::dualsense::TouchpadGestureProfile;
    using namespace std::chrono_literals;

    const auto defaults = parse({});
    assert(defaults.succeeded);
    assert(defaults.error.empty());
    assert(!defaults.options.deviceIndex);
    assert(!defaults.options.duration);
    assert(defaults.options.virtualBackend == Backend::Auto);
    assert(defaults.options.hapticThresholdPercent == 12);
    assert(defaults.options.touchpadProfile == Profile::None);

    const auto complete = parse({
        "3", "--seconds", "42", "--viiper", "C:\\Tools\\Viiper.exe",
        "--virtual-backend", "sidecar", "--telemetry-json", "result.json",
        "--xinput-index", "2", "--rumble", "--sync-lightbar",
        "--haptic-threshold", "25", "--verify-virtual-input",
        "--touchpad-profile", "warframe", "--apex-profile", "4",
        "--session-token", "0123456789abcdefABCDEF0123456789",
        "--session-owner-pid", "4294967295"});
    assert(complete.succeeded);
    assert(complete.options.deviceIndex == std::size_t{3});
    assert(complete.options.duration == 42s);
    assert(complete.options.viiperExecutable == "C:\\Tools\\Viiper.exe");
    assert(complete.options.virtualBackend == Backend::Sidecar);
    assert(complete.options.telemetryJson == "result.json");
    assert(complete.options.xinputIndex == 2U);
    assert(complete.options.routeRumble);
    assert(complete.options.syncLightbar);
    assert(complete.options.hapticThresholdPercent == 25);
    assert(complete.options.verifyVirtualInput);
    assert(complete.options.touchpadProfile == Profile::Warframe);
    assert(complete.options.apexProfileSlot == std::uint8_t{3});
    assert(complete.options.sessionOwnerProcessId == UINT32_MAX);

    // Compatibility flags remain accepted for pre-1.0 launchers, but are no
    // longer advertised because their behavior is now mandatory.
    assert(parse({"--proxy-xinput", "--isolate-apex"}).succeeded);
    const auto usage = asb::cli::bridgeCommandUsage();
    assert(usage.find("--proxy-xinput") == std::string_view::npos);
    assert(usage.find("--isolate-apex") == std::string_view::npos);

    assert(parse({"--virtual-backend", "integrated"}).options.virtualBackend ==
           Backend::Integrated);
    assert(parse({"--view-hold-swipe-up"}).options.touchpadProfile ==
           Profile::LegacyViewHoldSwipeUp);

    // Numeric options must consume their entire argument. std::stoul alone
    // accepts these malformed values, which could silently select a setting.
    assert(!parse({"--seconds", "12junk"}).succeeded);
    assert(!parse({"--xinput-index", "2junk"}).succeeded);
    assert(!parse({"7junk"}).succeeded);
    assert(!parse({"--apex-profile", "0"}).succeeded);
    assert(!parse({"--apex-profile", "5"}).succeeded);
    assert(!parse({"--haptic-threshold", "25"}).succeeded);
    assert(!parse({"--session-owner-pid", "42"}).succeeded);
    assert(!parse({"--session-token", "not-a-token"}).succeeded);
    assert(!parse({"--unknown"}).succeeded);

    return 0;
}
