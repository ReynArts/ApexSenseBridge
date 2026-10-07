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
    assert(defaults.options.apex6HapticGainPercent == 100);
    assert(!defaults.options.apex6HapticGainExplicit);
    assert(defaults.options.apex4GyroStrengthPercent == 100);
    assert(defaults.options.apex4GyroYawStrengthPercent == 100);
    assert(defaults.options.touchpadProfile == Profile::None);

    const auto complete = parse({
        "3", "--seconds", "42", "--viiper", "C:\\Tools\\Viiper.exe",
        "--virtual-backend", "sidecar", "--telemetry-json", "result.json",
        "--xinput-index", "2", "--rumble", "--sync-lightbar",
        "--haptic-threshold", "25", "--apex4-gyro-strength", "175",
        "--apex4-gyro-yaw-strength", "225", "--verify-virtual-input",
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
    assert(complete.options.apex4GyroStrengthPercent == 175);
    assert(complete.options.apex4GyroYawStrengthPercent == 225);
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
    const auto deathStranding = parse({"--touchpad-profile", "death-stranding-2"});
    assert(deathStranding.succeeded);
    assert(deathStranding.options.touchpadProfile == Profile::DeathStranding2);

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
    assert(parse({"--trigger-strength", "0", "--vibration-strength", "50"}).options.triggerStrengthPercent == 0);
    assert(parse({"--vibration-strength", "50"}).options.vibrationStrengthPercent == 50);
    assert(parse({"--vibration-strength", "200"}).options.vibrationStrengthPercent == 200);
    assert(!parse({"--vibration-strength", "201"}).succeeded);
    assert(!parse({"--trigger-strength", "101"}).succeeded);
    assert(!parse({"--vibration-strength", "-1"}).succeeded);
    assert(!parse({"--vibration-strength", "50junk"}).succeeded);
    assert(!parse({"--trigger-strength"}).succeeded);
    assert(!parse({"--apex4-gyro-strength", "24"}).succeeded);
    assert(!parse({"--apex4-gyro-yaw-strength", "401"}).succeeded);
    assert(!parse({"--apex4-gyro-strength", "100junk"}).succeeded);
    assert(!parse({"--apex4-gyro-yaw-strength"}).succeeded);
    for (const auto gain : {"100", "150", "200"}) {
        const auto parsed = parse({"--rumble", "--apex6-haptic-gain", gain});
        assert(parsed.succeeded && parsed.options.apex6HapticGainExplicit);
        assert(parsed.options.apex6HapticGainPercent == std::stoul(gain));
    }
    for (const auto gain : {"99", "201", "-1", "150junk", "150.5", "999999999999999999999999999"}) {
        assert(!parse({"--rumble", "--apex6-haptic-gain", gain}).succeeded);
    }
    assert(!parse({"--rumble", "--apex6-haptic-gain"}).succeeded);
    assert(!parse({"--apex6-haptic-gain", "150"}).succeeded);
    assert(usage.find("--apex6-haptic-gain 100..200") != std::string_view::npos);
    assert(usage.find("--vibration-strength 0..200") != std::string_view::npos);
    auto boosted = parse({"--rumble", "--vibration-strength", "150"}).options;
    std::string boostError;
    assert(asb::cli::applyControllerCalibration(boosted, "apex5", boostError));
    assert(boosted.vibrationStrengthPercent == 150);
    assert(asb::cli::applyControllerCalibration(boosted, "apex6", boostError));
    assert(boosted.vibrationStrengthPercent == 100);
    for (const auto model : {"apex4", "apex5"}) {
        auto options = parse({"--controller-calibration", std::string(model) + ":100:200:12:1:0:100:100"});
        assert(options.succeeded);
        assert(asb::cli::applyControllerCalibration(options.options, model, boostError));
        assert(options.options.vibrationStrengthPercent == 200);
    }
    assert(!parse({"--controller-calibration", "apex6:100:101:0:1:0:100:100"}).succeeded);
    assert(!parse({"--controller-calibration", "apex5:100:201:12:1:0:100:100"}).succeeded);

    const auto calibrated = parse({
        "--controller-calibration", "apex4:35:60:24:0:0:175:225",
        "--controller-calibration", "apex5:45:70:12:1:1:100:100",
        "--controller-calibration", "apex6:80:90:0:1:0:100:100",
        "--apex-profile", "3"});
    assert(calibrated.succeeded);
    for (const auto model : {"apex4", "apex5", "apex6"}) {
        auto options = calibrated.options;
        std::string error;
        assert(asb::cli::applyControllerCalibration(options, model, error));
        if (std::string_view(model) == "apex4") {
            assert(options.triggerStrengthPercent == 35 && options.vibrationStrengthPercent == 60);
            assert(options.hapticThresholdPercent == 24 && !options.routeRumble && !options.syncLightbar);
            assert(options.apex4GyroStrengthPercent == 175 && options.apex4GyroYawStrengthPercent == 225);
            assert(!options.apexProfileSlot);
        } else if (std::string_view(model) == "apex5") {
            assert(options.triggerStrengthPercent == 45 && options.vibrationStrengthPercent == 70);
            assert(options.routeRumble && options.syncLightbar && options.apexProfileSlot == 2);
        } else {
            assert(options.triggerStrengthPercent == 80 && options.vibrationStrengthPercent == 90);
            assert(options.routeRumble && !options.syncLightbar && options.hapticThresholdPercent == 0);
            assert(!options.apexProfileSlot && options.apex4GyroStrengthPercent == 100);
        }
    }
    auto partial = parse({"--controller-calibration", "apex5:45:70:12:1:1:100:100"}).options;
    std::string calibrationError;
    assert(!asb::cli::applyControllerCalibration(partial, "apex6", calibrationError));
    assert(partial.triggerStrengthPercent == 100); // Fail before any calibration is applied.
    assert(!asb::cli::applyControllerCalibration(partial, "unknown", calibrationError));
    auto legacy = complete.options;
    assert(asb::cli::applyControllerCalibration(legacy, "apex5", calibrationError));
    assert(legacy.triggerStrengthPercent == complete.options.triggerStrengthPercent && legacy.syncLightbar);
    for (const auto invalid : {
             "apex7:100:100:12:1:0:100:100", "apex4:101:100:12:1:0:100:100",
             "apex4:-1:100:12:1:0:100:100", "apex5:100junk:100:12:1:0:100:100",
             "apex6:100:100:12:1:0:100:100", "apex4:100:100:12:1:1:100:100",
             "apex5:100:100:12:1:0:200:100", "apex4:100:100:96:1:0:100:100",
             "apex4:100:100:12:2:0:100:100", "apex4:100:100:12:1:0:24:100",
             "apex4:100:100:12:1:0:100:401", "apex4:100:100:12:1:0:100:100:",
             "apex4:100:100:12:1:0:100:100:2", "apex4:100:100:12:1:0:100",
             "apex4:999999999999999999999999999:100:12:1:0:100:100"})
        assert(!parse({"--controller-calibration", invalid}).succeeded);
    assert(!parse({"--controller-calibration"}).succeeded);
    assert(!parse({"--controller-calibration", "apex4:100:100:12:1:0:100:100",
                   "--controller-calibration", "apex4:100:100:12:1:0:100:100"}).succeeded);

    return 0;
}
