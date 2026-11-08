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
    assert(defaults.options.reattachWindow == 600s);
    assert(defaults.options.virtualBackend == Backend::Auto);
    assert(defaults.options.hapticThresholdPercent == 12);
    assert(defaults.options.apex6HapticGainPercent == 100);
    assert(!defaults.options.apex6HapticGainExplicit);
    assert(defaults.options.apex4GyroStrengthPercent == 100);
    assert(defaults.options.apex4GyroYawStrengthPercent == 100);
    assert(defaults.options.touchpadProfile == Profile::None);
    assert(defaults.options.zoneEmulation);
    assert(defaults.options.lightbarBrightnessPercent == 100);

    const auto complete = parse({
        "3", "--seconds", "42", "--viiper", "C:\\Tools\\Viiper.exe",
        "--virtual-backend", "sidecar", "--telemetry-json", "result.json",
        "--xinput-index", "2", "--rumble", "--sync-lightbar",
        "--lightbar-brightness", "40",
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
    assert(complete.options.lightbarBrightnessPercent == 40);
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
    const auto zoneEmulation = parse({"--zone-emulation"});
    assert(zoneEmulation.succeeded);
    assert(zoneEmulation.options.zoneEmulation);
    const auto noZoneEmulation = parse({"--no-zone-emulation"});
    assert(noZoneEmulation.succeeded);
    assert(!noZoneEmulation.options.zoneEmulation);
    assert(!parse({"--no-zone-emulation", "--rumble"}).options.zoneEmulation);
    assert(parse({"--no-zone-emulation", "--zone-emulation"}).options.zoneEmulation);
    const auto deathStranding = parse({"--touchpad-profile", "death-stranding-2"});
    assert(deathStranding.succeeded);
    assert(deathStranding.options.touchpadProfile == Profile::DeathStranding2);
    const auto alanWake = parse({"--touchpad-profile", "alan-wake-2"});
    assert(alanWake.succeeded);
    assert(alanWake.options.touchpadProfile == Profile::AlanWake2);

    // Numeric options must consume their entire argument. std::stoul alone
    // accepts these malformed values, which could silently select a setting.
    assert(!parse({"--seconds", "12junk"}).succeeded);
    assert(parse({"--reattach-seconds", "0"}).options.reattachWindow == 0s);
    assert(parse({"--reattach-seconds", "90"}).options.reattachWindow == 90s);
    assert(parse({"--reattach-seconds", "3600"}).succeeded);
    assert(!parse({"--reattach-seconds", "3601"}).succeeded);
    assert(!parse({"--reattach-seconds", "-5"}).succeeded);
    assert(!parse({"--reattach-seconds", "12junk"}).succeeded);
    assert(!parse({"--reattach-seconds"}).succeeded);
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
    assert(defaults.options.apex6SampleRateHz == 1000 && !defaults.options.apex6SampleRateExplicit);
    for (const auto rate : {"1000", "2000"}) {
        const auto parsed = parse({"--rumble", "--apex6-rate", rate});
        assert(parsed.succeeded && parsed.options.apex6SampleRateExplicit);
        assert(parsed.options.apex6SampleRateHz == std::stoul(rate));
    }
    assert(parse({"--apex6-rate", "1000"}).succeeded);
    assert(!parse({"--apex6-rate", "2000"}).succeeded);
    for (const auto rate : {"0", "500", "1500", "2001", "2000junk", "-2000", "2000.0", ""}) {
        assert(!parse({"--rumble", "--apex6-rate", rate}).succeeded);
    }
    assert(!parse({"--rumble", "--apex6-rate"}).succeeded);
    assert(usage.find("--apex6-rate 1000|2000") != std::string_view::npos);
    assert(defaults.options.apex6GripDuckPercent == 0 && !defaults.options.apex6GripDuckExplicit);
    assert(defaults.options.apex6TriggerCeilingPercent == 100 &&
           !defaults.options.apex6TriggerCeilingExplicit);
    if (asb::cli::kApex6ShapingOptionsEnabled) {
        for (const auto duck : {"0", "35", "100"}) {
            const auto parsed = parse({"--rumble", "--apex6-grip-duck", duck});
            assert(parsed.succeeded && parsed.options.apex6GripDuckExplicit);
            assert(parsed.options.apex6GripDuckPercent == std::stoul(duck));
        }
        for (const auto duck : {"101", "-1", "10junk", "10.5", "", "999999999999999999999999999"}) {
            assert(!parse({"--rumble", "--apex6-grip-duck", duck}).succeeded);
        }
        assert(!parse({"--rumble", "--apex6-grip-duck"}).succeeded);
        assert(!parse({"--apex6-grip-duck", "50"}).succeeded);
        for (const auto ceiling : {"20", "80", "100"}) {
            const auto parsed = parse({"--apex6-trigger-ceiling", ceiling});
            assert(parsed.succeeded && parsed.options.apex6TriggerCeilingExplicit);
            assert(parsed.options.apex6TriggerCeilingPercent == std::stoul(ceiling));
        }
        for (const auto ceiling : {"19", "101", "-1", "50junk", "50.5", ""}) {
            assert(!parse({"--apex6-trigger-ceiling", ceiling}).succeeded);
        }
        assert(!parse({"--apex6-trigger-ceiling"}).succeeded);
    } else {
        assert(!parse({"--rumble", "--apex6-grip-duck", "35"}).succeeded);
        assert(!parse({"--apex6-trigger-ceiling", "80"}).succeeded);
        assert(usage.find("--apex6-grip-duck") == std::string_view::npos);
        assert(usage.find("--apex6-trigger-ceiling") == std::string_view::npos);
    }
    // The lightbar brightness limit is a plain percentage; it is accepted
    // without --sync-lightbar so launchers can always pass it.
    for (const auto brightness : {"0", "55", "100"}) {
        const auto parsed = parse({"--lightbar-brightness", brightness});
        assert(parsed.succeeded);
        assert(parsed.options.lightbarBrightnessPercent == std::stoul(brightness));
    }
    for (const auto brightness : {"101", "-1", "50junk", "50.5", "999999999999999999999999999"}) {
        assert(!parse({"--sync-lightbar", "--lightbar-brightness", brightness}).succeeded);
    }
    assert(!parse({"--lightbar-brightness"}).succeeded);
    assert(usage.find("--lightbar-brightness 0..100") != std::string_view::npos);
    assert(usage.find("YAW[:RGBBRIGHTNESS]") != std::string_view::npos);
    assert(usage.find("--vibration-strength 0..200") != std::string_view::npos);
    assert(usage.find("[--no-zone-emulation]") != std::string_view::npos);
    assert(usage.find("[--zone-emulation]") == std::string_view::npos);
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
    for (const auto model : {"apex4", "apex5", "apex6"}) {
        auto trayStyle = parse({"--apex6-rate", "2000",
                                "--controller-calibration", "apex4:100:100:12:1:0:100:100",
                                "--controller-calibration", "apex5:100:100:12:1:0:100:100",
                                "--controller-calibration", "apex6:100:100:0:1:0:100:100"});
        assert(trayStyle.succeeded && trayStyle.options.apex6SampleRateHz == 2000);
        assert(asb::cli::applyControllerCalibration(trayStyle.options, model, boostError));
        if (std::string_view(model) == "apex6") {
            assert(trayStyle.options.apex6SampleRateHz == 2000 &&
                   trayStyle.options.apex6SampleRateExplicit);
        } else {
            assert(trayStyle.options.apex6SampleRateHz == 1000 &&
                   !trayStyle.options.apex6SampleRateExplicit);
        }
    }
    auto noRumble = parse({"--apex6-rate", "2000",
                           "--controller-calibration", "apex6:100:100:0:0:0:100:100"});
    assert(noRumble.succeeded);
    assert(asb::cli::applyControllerCalibration(noRumble.options, "apex6", boostError));
    assert(noRumble.options.apex6SampleRateHz == 1000 && !noRumble.options.apex6SampleRateExplicit);
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
            // Seven fields keep the default brightness limit.
            assert(options.lightbarBrightnessPercent == 100);
        } else {
            assert(options.triggerStrengthPercent == 80 && options.vibrationStrengthPercent == 90);
            assert(options.routeRumble && !options.syncLightbar && options.hapticThresholdPercent == 0);
            assert(options.apexProfileSlot == 2 && options.apex4GyroStrengthPercent == 100);
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
    assert(legacy.lightbarBrightnessPercent == 40);
    // The optional eighth field carries the brightness limit; the calibration
    // of the verified model replaces any plain --lightbar-brightness.
    auto limited = parse({"--lightbar-brightness", "40",
                          "--controller-calibration", "apex5:45:70:12:1:1:100:100:35",
                          "--controller-calibration", "apex4:35:60:24:0:0:175:225:100"});
    assert(limited.succeeded);
    assert(limited.options.controllerCalibrations[1]->rgbBrightness == 35);
    assert(asb::cli::applyControllerCalibration(limited.options, "apex5", calibrationError));
    assert(limited.options.lightbarBrightnessPercent == 35);
    auto limitedApex4 = limited.options;
    assert(asb::cli::applyControllerCalibration(limitedApex4, "apex4", calibrationError));
    assert(limitedApex4.lightbarBrightnessPercent == 100 && !limitedApex4.syncLightbar);
    for (const auto brightness : {"0", "100"}) {
        assert(parse({"--controller-calibration",
                      std::string("apex5:45:70:12:1:1:100:100:") + brightness}).succeeded);
    }
    for (const auto invalid : {
             "apex5:45:70:12:1:1:100:100:101", "apex5:45:70:12:1:1:100:100:-1",
             "apex5:45:70:12:1:1:100:100:4x", "apex5:45:70:12:1:1:100:100:",
             "apex5:45:70:12:1:1:100:100:40:1", "apex4:35:60:24:0:0:175:225:40",
             "apex6:80:90:0:1:0:100:100:99"})
        assert(!parse({"--controller-calibration", invalid}).succeeded);
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
