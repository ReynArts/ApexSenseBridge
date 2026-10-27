#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "cli/JsonSupport.h"
#include "core/TriggerResetGuard.h"
#include "dualsense/AdaptiveTriggerBridge.h"
#include "dualsense/AdaptiveTriggerTranslation.h"
#include "dualsense/DualSenseFeedback.h"
#include "flydigi/Apex5Device.h"
#include "platform/SessionControl.h"
#include "platform/XInputGamepad.h"
#include "tools/TriggerPositionMarkers.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
std::atomic_bool stopRequested{false};

BOOL WINAPI consoleHandler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        stopRequested.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}

std::int64_t elapsedUs(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
}

template <typename Range>
void writeBytes(std::ostream& output, const Range& bytes) {
    output << '[';
    bool first = true;
    for (const auto byte : bytes) {
        if (!first) output << ',';
        output << static_cast<unsigned>(byte);
        first = false;
    }
    output << ']';
}

struct WriteRecord {
    std::int64_t beginUs;
    std::int64_t endUs;
    std::vector<std::uint8_t> bytes;
    bool success;
    std::string error;
};

class RecordingTransport final : public asb::platform::HidTransport {
public:
    RecordingTransport(asb::flydigi::TransportPtr transport, Clock::time_point start,
                       std::vector<WriteRecord>& records)
        : transport_(std::move(transport)), start_(start), records_(records) {}

    bool isOpen() const noexcept override { return transport_->isOpen(); }
    const asb::HidDeviceInfo& info() const noexcept override { return transport_->info(); }
    bool writeOutputReport(std::span<const std::uint8_t> report, std::string& error) override {
        const auto begin = elapsedUs(start_);
        const bool success = transport_->writeOutputReport(report, error);
        const auto end = elapsedUs(start_);
        records_.push_back({begin, end, {report.begin(), report.end()}, success,
                            success ? std::string{} : error});
        return success;
    }
    bool readFeatureReport(std::span<std::uint8_t> report, std::string& error) override {
        return transport_->readFeatureReport(report, error);
    }
    asb::platform::HidReadStatus readInputReport(std::span<std::uint8_t> report,
        std::chrono::milliseconds timeout, std::size_t& bytesRead, std::string& error) override {
        return transport_->readInputReport(report, timeout, bytesRead, error);
    }

private:
    asb::flydigi::TransportPtr transport_;
    Clock::time_point start_;
    std::vector<WriteRecord>& records_;
};

struct Phase {
    std::string_view name;
    std::array<std::uint8_t, 11> native;
    bool dynamic = false;
};

class PreciseTimer {
public:
    PreciseTimer() {
        constexpr DWORD kHighResolution = 0x00000002;
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, kHighResolution, TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!timer_) timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
    }
    ~PreciseTimer() {
        if (timer_) CloseHandle(timer_);
    }
    PreciseTimer(const PreciseTimer&) = delete;
    PreciseTimer& operator=(const PreciseTimer&) = delete;

    void waitUntil(Clock::time_point deadline) const {
        const auto remaining = deadline - Clock::now();
        if (remaining <= Clock::duration::zero()) return;
        if (timer_) {
            LARGE_INTEGER due{};
            due.QuadPart = -static_cast<LONGLONG>(
                (std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() + 99) / 100);
            if (due.QuadPart == 0) due.QuadPart = -1;
            if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer_, INFINITE);
                return;
            }
        }
        std::this_thread::sleep_until(deadline);
    }

private:
    HANDLE timer_ = nullptr;
};

struct PulseLadderRunner {
    std::thread worker;
    std::atomic_bool stop{false};
    bool failed = false;
    std::string error;

    PulseLadderRunner() = default;
    PulseLadderRunner(const PulseLadderRunner&) = delete;
    PulseLadderRunner& operator=(const PulseLadderRunner&) = delete;
    ~PulseLadderRunner() { join(); }

    void join() {
        stop.store(true);
        if (worker.joinable()) worker.join();
    }

    void start(asb::flydigi::Apex5Device& device, const asb::dualsense::ZoneUnits& units, bool kick) {
        worker = std::thread([this, &device, units, kick] {
            constexpr std::array<unsigned, 5> widths{400, 200, 100, 60, 40};
            const PreciseTimer timer;
            const asb::ForceTriggerCommand on = kick
                ? asb::ForceTriggerCommand{asb::TriggerSide::Right, asb::TriggerMode::Race,
                                           {0, units.gallopingAmplitude, 0, 0, 0}}
                : asb::ForceTriggerCommand{asb::TriggerSide::Right, asb::TriggerMode::RecoilRattle,
                                           {0, 1, units.gallopingAmplitude, units.gallopingCarrier, 0}};
            const asb::ForceTriggerCommand off{asb::TriggerSide::Right, asb::TriggerMode::Normal, {}};
            const auto waitOrStop = [&](Clock::time_point deadline) {
                while (!stop.load() && Clock::now() < deadline) {
                    timer.waitUntil((std::min)(deadline, Clock::now() + std::chrono::milliseconds(20)));
                }
                return !stop.load();
            };
            const auto send = [&](const asb::ForceTriggerCommand& command) {
                std::string writeError;
                if (device.setTriggerRaw(command, writeError)) return true;
                failed = true;
                error = writeError;
                return false;
            };
            auto at = Clock::now() + std::chrono::milliseconds(2000);
            for (const auto width : widths) {
                for (unsigned pulse = 0; pulse < 3; ++pulse) {
                    if (!waitOrStop(at) || !send(on)) goto finished;
                    const auto end = at + std::chrono::milliseconds(width);
                    if (!waitOrStop(end) || !send(off)) goto finished;
                    at = end + std::chrono::milliseconds(700);
                }
                at += std::chrono::milliseconds(800);
            }
        finished:
            std::string ignored;
            device.setTriggerRaw(off, ignored);
        });
    }
};

struct AsyncWriteStop {
    asb::flydigi::Apex5Device& device;
    ~AsyncWriteStop() { device.stopAsyncWrites(); }
};

constexpr std::array<Phase, 5> phases{{
    {"normal", {5}},
    {"feedback_start38_force8", {0x21, 0xFC, 0x03}},
    {"weapon_start38_length39_force8", {0x25, 0x14}},
    {"weapon_start38_length96_force8", {0x25, 0x84}},
    {"vibration_start77_amplitude15_frequency10", {0x26, 0xF0, 0x03, 0, 0, 0, 0, 0, 0, 10}}
}};

constexpr std::array<Phase, 3> vibrationPhases{{
    {"normal", {5}},
    {"vibration_start77_amplitude30_frequency20", {0x26, 0xF0, 0x03, 0, 0x90, 0x24, 0x09, 0, 0, 20}},
    {"vibration_start77_amplitude60_frequency20", {0x26, 0xF0, 0x03, 0, 0xB0, 0x6D, 0x1B, 0, 0, 20}}
}};

constexpr std::array<Phase, 7> legacyPhases{{
    {"normal", {5}},
    {"legacy11_start38_strength10", {0x11, 38, 10}},
    {"legacy11_start38_strength5", {0x11, 38, 5}},
    {"legacy11_start38_strength1", {0x11, 38, 1}},
    {"legacy12_start38_end134_strength10", {0x12, 38, 134, 10}},
    {"legacy12_start38_end77_strength5", {0x12, 38, 77, 5}},
    {"legacy02_start38_end134_strength40", {0x02, 38, 134, 40}}
}};

constexpr std::array<Phase, 6> bowPhases{{
    {"normal", {5}},
    {"bow_emulated_draw4_race_then_break", {0x22, 0x44, 0x00, 0x0B}},
    {"bow_start2_end6_draw4_snap2", {0x22, 0x44, 0x00, 0x0B}},
    {"bow_start2_end6_draw4_snap8", {0x22, 0x44, 0x00, 0x3B}},
    {"bow_start2_end6_draw8_snap2", {0x22, 0x44, 0x00, 0x0F}},
    {"bow_start2_end6_draw8_snap8", {0x22, 0x44, 0x00, 0x3F}}
}};

constexpr std::array<Phase, 18> zonePhases{{
    {"normal", {5}},
    {"pulse_ladder", {0x23, 0x01, 0x02, 0x13, 2}},
    {"galloping_heartbeat_static", {0x23, 0x01, 0x02, 0x0B, 1}},
    {"galloping_heartbeat_dynamic", {0x23, 0x01, 0x02, 0x0B, 1}, true},
    {"galloping_horse_static", {0x23, 0x01, 0x02, 0x13, 2}},
    {"galloping_horse_dynamic", {0x23, 0x01, 0x02, 0x13, 2}, true},
    {"machine_gun", {0x27, 0x02, 0x02, 0x3F, 9, 1}},
    {"machine_burst_static", {0x27, 0x02, 0x02, 0x07, 18, 12}},
    {"machine_burst_dynamic", {0x27, 0x02, 0x02, 0x07, 18, 12}, true},
    {"ramp_static", {0x21, 0xFE, 0x03, 0x40, 0xB4, 0xB1, 0x3E}},
    {"ramp_dynamic", {0x21, 0xFE, 0x03, 0x40, 0xB4, 0xB1, 0x3E}, true},
    {"ramp0_static", {0x21, 0xFF, 0x03, 0x88, 0x34, 0xB6, 0x3E}},
    {"ramp0_dynamic", {0x21, 0xFF, 0x03, 0x88, 0x34, 0xB6, 0x3E}, true},
    {"wall_static", {0x21, 0x1C, 0x00, 0xC0, 0x7F}},
    {"wall_dynamic", {0x21, 0x1C, 0x00, 0xC0, 0x7F}, true},
    {"vibramp_static", {0x26, 0xFE, 0x03, 0x40, 0xB4, 0xB1, 0x3E, 0, 0, 45}},
    {"vibramp_dynamic", {0x26, 0xFE, 0x03, 0x40, 0xB4, 0xB1, 0x3E, 0, 0, 45}, true}
}};

constexpr std::array<Phase, 12> replayPhases{{
    {"normal", {5}},
    {"issue58_feedback_zone9_strength1", {0x21, 0x00, 0x02}, true},
    {"issue58_vibration_all_zones_amplitude2_f180", {0x26, 0xFF, 0x03, 0x49, 0x92, 0x24, 0x09, 0, 0, 0xB4}, true},
    {"issue58_feedback_zones6to9_strength1", {0x21, 0xC0, 0x03}, true},
    {"issue58_vibration_all_zones_amplitude2_f45", {0x26, 0xFF, 0x03, 0x49, 0x92, 0x24, 0x09, 0, 0, 45}, true},
    {"issue58_vibration_all_zones_amplitude6_f180", {0x26, 0xFF, 0x03, 0x6D, 0xDB, 0xB6, 0x2D, 0, 0, 0xB4}, true},
    {"issue35_feedback_zones7to9_strength1", {0x21, 0x80, 0x03}, true},
    {"issue51_bow_zones0to3_draw2_snap8", {0x22, 0x09, 0x00, 0x39}, true},
    {"issue51_bow_v102_emulation", {0x22, 0x09, 0x00, 0x09}},
    {"issue51_break_zones0to3_force64", {0x02, 0, 58, 64}},
    {"issue40_bow_zones0to8_draw1_snap8", {0x22, 0x01, 0x01, 0x38}, true},
    {"issue40_bow_v102_emulation", {0x22, 0x01, 0x01, 0x00}}
}};

struct ManualBow {
    std::string_view phase;
    asb::ForceTriggerCommand rupture;
    unsigned switchAxis;
    unsigned rearmAxis;
};

constexpr std::array<ManualBow, 2> replayManualBows{{
    {"issue51_bow_v102_emulation", {asb::TriggerSide::Right, asb::TriggerMode::SniperBreak, {58, 40, 16, 0, 0}}, 65, 10},
    {"issue40_bow_v102_emulation", {asb::TriggerSide::Right, asb::TriggerMode::SniperBreak, {154, 40, 8, 0, 0}}, 173, 10}
}};

constexpr std::array<Phase, 18> ladderPhases{{
    {"normal", {5}},
    {"race_force_4", {0x01, 0, 4}},
    {"gap", {5}},
    {"race_force_8", {0x01, 0, 8}},
    {"gap", {5}},
    {"race_force_16", {0x01, 0, 16}},
    {"gap", {5}},
    {"race_force_32", {0x01, 0, 32}},
    {"gap", {5}},
    {"race_force_64", {0x01, 0, 64}},
    {"gap", {5}},
    {"race_force_96", {0x01, 0, 96}},
    {"gap", {5}},
    {"race_force_128", {0x01, 0, 128}},
    {"gap", {5}},
    {"race_force_192", {0x01, 0, 192}},
    {"gap", {5}},
    {"normal", {5}}
}};

constexpr unsigned kLadderGapSeconds = 2;
constexpr unsigned kLadderNormalMaxSeconds = 3;
constexpr unsigned kLadderMinForce = 1;
constexpr unsigned kLadderMaxForce = 192;
constexpr std::size_t kLadderMaxForceCount = 12;
constexpr std::array<unsigned, 8> kDefaultLadderForces{4, 8, 16, 32, 64, 96, 128, 192};

std::vector<unsigned> parseLadderForces(std::string_view text) {
    const std::string message = "--ladder-forces must be a comma-separated list of 1 to " +
        std::to_string(kLadderMaxForceCount) + " integers " + std::to_string(kLadderMinForce) + ".." +
        std::to_string(kLadderMaxForce);
    std::vector<unsigned> forces;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = text.find(',', start);
        const std::string_view item = text.substr(start, comma == std::string_view::npos ? comma : comma - start);
        const bool digitsOnly = !item.empty() &&
            std::all_of(item.begin(), item.end(), [](char character) { return character >= '0' && character <= '9'; });
        unsigned value = 0;
        if (digitsOnly) {
            const auto parsed = std::from_chars(item.data(), item.data() + item.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != item.data() + item.size()) value = 0;
        }
        if (value < kLadderMinForce || value > kLadderMaxForce || forces.size() >= kLadderMaxForceCount) {
            throw std::runtime_error(message);
        }
        forces.push_back(value);
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return forces;
}

constexpr unsigned kMinZoneForceUnit = 8;
constexpr unsigned kMaxZoneForceUnit = 24;
constexpr unsigned kMinZoneVibrationUnit = 10;
constexpr unsigned kMaxZoneVibrationUnit = 20;

std::uint8_t parseUnitOption(std::string_view name, std::string_view text, unsigned minimum, unsigned maximum) {
    unsigned value = 0;
    const char* const first = text.data();
    const char* const last = first + text.size();
    const auto parsed = std::from_chars(first, last, value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != last || value < minimum || value > maximum) {
        throw std::runtime_error(std::string(name) + " must be an integer " + std::to_string(minimum) +
                                 ".." + std::to_string(maximum));
    }
    return static_cast<std::uint8_t>(value);
}

std::array<std::uint8_t, 8> parseZoneForceTable(std::string_view text) {
    const std::string message = "--zone-force-table must be 8 comma-separated integers " +
        std::to_string(kLadderMinForce) + ".." + std::to_string(kLadderMaxForce) + " in non-decreasing order";
    std::vector<unsigned> values;
    try {
        values = parseLadderForces(text);
    } catch (const std::runtime_error&) {
        throw std::runtime_error(message);
    }
    if (values.size() != 8 || !std::is_sorted(values.begin(), values.end())) throw std::runtime_error(message);
    std::array<std::uint8_t, 8> table{};
    std::transform(values.begin(), values.end(), table.begin(),
                   [](unsigned value) { return static_cast<std::uint8_t>(value); });
    return table;
}

constexpr unsigned kMaxZoneVibrationTableValue = 160;

std::array<std::uint8_t, 8> parseZoneVibrationTable(std::string_view text) {
    const std::string message = "--zone-vibration-table must be 8 comma-separated integers " +
        std::to_string(kLadderMinForce) + ".." + std::to_string(kMaxZoneVibrationTableValue) +
        " in non-decreasing order";
    std::vector<unsigned> values;
    try {
        values = parseLadderForces(text);
    } catch (const std::runtime_error&) {
        throw std::runtime_error(message);
    }
    if (values.size() != 8 || !std::is_sorted(values.begin(), values.end()) ||
        values.back() > kMaxZoneVibrationTableValue) {
        throw std::runtime_error(message);
    }
    std::array<std::uint8_t, 8> table{};
    std::transform(values.begin(), values.end(), table.begin(),
                   [](unsigned value) { return static_cast<std::uint8_t>(value); });
    return table;
}

asb::ForceTriggerCommand scaleZoneStaticCommand(asb::ForceTriggerCommand command,
                                                asb::dualsense::ZoneUnits units) {
    if (command.mode == asb::TriggerMode::Race) {
        const unsigned strength = static_cast<unsigned>(command.params[1]) / 8U;
        if (strength >= 1U && strength <= units.forceTable.size() && units.forceTable[strength - 1U] != 0) {
            command.params[1] = units.forceTable[strength - 1U];
        } else if (units.force != kMinZoneForceUnit) {
            command.params[1] = static_cast<std::uint8_t>(
                (std::min)(255U, static_cast<unsigned>(command.params[1]) / 8U * units.force));
        }
    } else if (command.mode == asb::TriggerMode::RecoilRattle && units.vibration != kMinZoneVibrationUnit) {
        command.params[2] = static_cast<std::uint8_t>(
            (std::min)(255U, static_cast<unsigned>(command.params[2]) * units.vibration / 10U));
    }
    return command;
}

constexpr std::array<Phase, 8> positionPhases{{
    {"feedback_start0_force8", {0x21, 0xFF, 0x03}},
    {"feedback_start38_force8", {0x21, 0xFC, 0x03}},
    {"feedback_start77_force8", {0x21, 0xF0, 0x03}},
    {"feedback_start115_force8", {0x21, 0xC0, 0x03}},
    {"feedback_start154_force8", {0x21, 0x00, 0x03}},
    {"weapon_start38_length20_force8", {0x25, 0x0C}},
    {"weapon_start38_length39_force8", {0x25, 0x14}},
    {"weapon_start38_length96_force8", {0x25, 0x84}}
}};

struct PositionMarker {
    std::int64_t elapsed;
    unsigned phase;
    std::uint8_t right;
    std::string_view label;
    std::string_view button;
};

struct Sample {
    std::int64_t elapsed;
    unsigned phase;
    std::uint8_t left;
    std::uint8_t right;
    std::uint16_t buttons;
};

struct PhaseRecord {
    std::int64_t elapsed;
    unsigned phase;
    asb::ForceTriggerCommand command;
};

int run(int argc, char** argv) {
    std::filesystem::path outputPath;
    unsigned seconds = 6;
    unsigned bowSwitchAxis = 130;
    bool activate = false;
    bool plan = false;
    bool vibrationCheck = false;
    bool positionCheck = false;
    bool legacyCheck = false;
    bool bowCheck = false;
    bool zoneCheck = false;
    bool ladderCheck = false;
    bool replayCheck = false;
    bool markPositions = false;
    bool waitReady = false;
    bool markerTrial = false;
    bool snapTrial = false;
    bool zoneUnitOptionGiven = false;
    bool zoneForceUnitGiven = false;
    bool zoneForceLawGiven = false;
    bool perceptualForceLaw = false;
    bool zoneForceTableGiven = false;
    bool zoneVibrationTableGiven = false;
    bool zoneVibrationUnitGiven = false;
    std::optional<std::uint8_t> zoneVibrationFrequency;
    bool gallopingOptionGiven = false;
    bool pulseKick = false;
    asb::dualsense::ZoneUnits zoneUnits{};
    std::string singleEffect;
    std::optional<std::vector<unsigned>> ladderForces;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--activate") activate = true;
        else if (option == "--plan") plan = true;
        else if (option == "--vibration-check") vibrationCheck = true;
        else if (option == "--position-check") positionCheck = true;
        else if (option == "--legacy-check") legacyCheck = true;
        else if (option == "--bow-check") bowCheck = true;
        else if (option == "--zone-check") zoneCheck = true;
        else if (option == "--ladder-check") ladderCheck = true;
        else if (option == "--replay-check") replayCheck = true;
        else if (option == "--ladder-forces" && index + 1 < argc) ladderForces = parseLadderForces(argv[++index]);
        else if (option == "--bow-switch-axis" && index + 1 < argc) {
            const auto parsed = std::stoul(argv[++index]);
            if (parsed < 40 || parsed > 250) throw std::runtime_error("--bow-switch-axis must be 40..250");
            bowSwitchAxis = static_cast<unsigned>(parsed);
        }
        else if (option == "--zone-force-unit" && index + 1 < argc) {
            zoneUnits.force = parseUnitOption(option, argv[++index], kMinZoneForceUnit, kMaxZoneForceUnit);
            zoneUnitOptionGiven = true;
            zoneForceUnitGiven = true;
        }
        else if (option == "--zone-force-law" && index + 1 < argc) {
            const std::string_view law = argv[++index];
            if (law != "linear" && law != "perceptual") {
                throw std::runtime_error("--zone-force-law must be linear or perceptual");
            }
            zoneForceLawGiven = true;
            perceptualForceLaw = law == "perceptual";
        }
        else if (option == "--zone-force-table" && index + 1 < argc) {
            zoneUnits.forceTable = parseZoneForceTable(argv[++index]);
            zoneForceTableGiven = true;
        }
        else if (option == "--zone-vibration-unit" && index + 1 < argc) {
            zoneUnits.vibration = parseUnitOption(option, argv[++index], kMinZoneVibrationUnit, kMaxZoneVibrationUnit);
            zoneUnitOptionGiven = true;
            zoneVibrationUnitGiven = true;
        }
        else if (option == "--pulse-kick") {
            pulseKick = true;
            gallopingOptionGiven = true;
        }
        else if (option == "--machine-floor" && index + 1 < argc) {
            zoneUnits.machineAmplitudeFloor = parseUnitOption(option, argv[++index], 0, 120);
            gallopingOptionGiven = true;
        }
        else if (option == "--gallop-min-beat-ms" && index + 1 < argc) {
            zoneUnits.gallopingMinimumBeatSeconds = parseUnitOption(option, argv[++index], 0, 250) / 1000.0;
            gallopingOptionGiven = true;
        }
        else if (option == "--gallop-frequency" && index + 1 < argc) {
            zoneUnits.gallopingCarrier = parseUnitOption(option, argv[++index], 10, 120);
            gallopingOptionGiven = true;
        }
        else if (option == "--gallop-amplitude" && index + 1 < argc) {
            zoneUnits.gallopingAmplitude = parseUnitOption(option, argv[++index], 10, 160);
            gallopingOptionGiven = true;
        }
        else if (option == "--gallop-beat-slots" && index + 1 < argc) {
            const std::string text = argv[++index];
            std::size_t consumed = 0;
            double parsed = 0.0;
            try {
                parsed = std::stod(text, &consumed);
            } catch (const std::exception&) {
                consumed = 0;
            }
            if (consumed != text.size() || !(parsed >= 0.2 && parsed <= 1.0)) {
                throw std::runtime_error("--gallop-beat-slots must be a number 0.2..1.0");
            }
            zoneUnits.gallopingBeatSlots = parsed;
            gallopingOptionGiven = true;
        }
        else if (option == "--zone-vibration-frequency" && index + 1 < argc) {
            zoneVibrationFrequency = parseUnitOption(option, argv[++index], 10, 120);
        }
        else if (option == "--zone-vibration-table" && index + 1 < argc) {
            zoneUnits.vibrationTable = parseZoneVibrationTable(argv[++index]);
            zoneVibrationTableGiven = true;
        }
        else if (option == "--mark-positions") markPositions = true;
        else if (option == "--wait-ready") waitReady = true;
        else if (option == "--marker-trial") { markerTrial = true; markPositions = true; waitReady = true; }
        else if (option == "--snap-trial") {
            snapTrial = true; markerTrial = true; markPositions = true; waitReady = true; positionCheck = true;
        }
        else if (option == "--single-effect" && index + 1 < argc) singleEffect = argv[++index];
        else if (option == "--output" && index + 1 < argc) outputPath = argv[++index];
        else if (option == "--phase-seconds" && index + 1 < argc) {
            const std::string value = argv[++index];
            std::size_t consumed = 0;
            const auto parsed = std::stoul(value, &consumed);
            if (consumed != value.size() || parsed < 1 || parsed > 45) {
                throw std::runtime_error("--phase-seconds must be 1..45");
            }
            seconds = static_cast<unsigned>(parsed);
        } else {
            std::cerr << "Usage: Apex45TriggerCapture --output FILE [--activate] "
                         "[--vibration-check | --position-check | --legacy-check | --bow-check | --zone-check | --ladder-check | --replay-check (effects logged in issues #35, #40, #51 and #58, sent through the production bridge)] [--bow-switch-axis 40..250] [--single-effect PHASE_NAME] "
                         "[--ladder-forces 1..192,... (1 to 12 comma-separated integers, only with --ladder-check)] "
                         "[--zone-force-unit 8..24] [--zone-vibration-unit 10..20] [--zone-force-law linear|perceptual, "
                         "perceptual excludes --zone-force-unit; all three only with --zone-check] "
                         "[--zone-force-table f1,...,f8 (8 non-decreasing integers 1..192, only with --zone-check, excludes --zone-force-law and --zone-force-unit)] "
                         "[--zone-vibration-table a1,...,a8 (8 non-decreasing integers 1..160, only with --zone-check, excludes --zone-vibration-unit)] "
                         "[--zone-vibration-frequency 10..120 (only with --zone-check, overrides the frequency of the vibramp phases)] "
                         "[--gallop-frequency 10..120] [--gallop-amplitude 10..160] [--gallop-beat-slots 0.2..1.0] [--gallop-min-beat-ms 0..250] [--machine-floor 0..120] [--pulse-kick] (galloping beats and the pulse_ladder phase; --pulse-kick sends Race force kicks of --gallop-amplitude instead of vibration; only with --zone-check) "
                         "[--mark-positions] [--wait-ready] [--marker-trial | --snap-trial] [--phase-seconds 1..10, or 1..45 with --legacy-check, --bow-check, --zone-check, --ladder-check or --replay-check] [--plan]\n";
            return 1;
        }
    }
    if (static_cast<int>(positionCheck) + static_cast<int>(vibrationCheck) + static_cast<int>(legacyCheck) +
        static_cast<int>(bowCheck) + static_cast<int>(zoneCheck) + static_cast<int>(ladderCheck) +
        static_cast<int>(replayCheck) > 1) {
        throw std::runtime_error("Choose only one check family");
    }
    if (ladderForces && !ladderCheck) {
        throw std::runtime_error("--ladder-forces requires --ladder-check");
    }
    if (zoneUnitOptionGiven && !zoneCheck) {
        throw std::runtime_error("--zone-force-unit and --zone-vibration-unit require --zone-check");
    }
    if (zoneForceLawGiven && !zoneCheck) {
        throw std::runtime_error("--zone-force-law requires --zone-check");
    }
    if (perceptualForceLaw && zoneForceUnitGiven) {
        throw std::runtime_error("--zone-force-law perceptual cannot be combined with --zone-force-unit");
    }
    if (gallopingOptionGiven && !zoneCheck) {
        throw std::runtime_error("--gallop-frequency, --gallop-amplitude and --gallop-beat-slots require --zone-check");
    }
    if (zoneVibrationFrequency && !zoneCheck) {
        throw std::runtime_error("--zone-vibration-frequency requires --zone-check");
    }
    if (zoneVibrationTableGiven && !zoneCheck) {
        throw std::runtime_error("--zone-vibration-table requires --zone-check");
    }
    if (zoneVibrationTableGiven && zoneVibrationUnitGiven) {
        throw std::runtime_error("--zone-vibration-table cannot be combined with --zone-vibration-unit");
    }
    if (zoneForceTableGiven && !zoneCheck) {
        throw std::runtime_error("--zone-force-table requires --zone-check");
    }
    if (zoneForceTableGiven && (perceptualForceLaw || zoneForceUnitGiven)) {
        throw std::runtime_error("--zone-force-table cannot be combined with --zone-force-law or --zone-force-unit");
    }
    if (perceptualForceLaw) {
        zoneUnits.forceTable = asb::dualsense::kPerceptualForceTable;
        if (!zoneVibrationTableGiven && !zoneVibrationUnitGiven) {
            zoneUnits.vibrationTable = asb::dualsense::kPerceptualVibrationTable;
        }
    }
    if (replayCheck) zoneUnits = asb::dualsense::productionZoneUnits();
    const bool longCheck = legacyCheck || bowCheck || zoneCheck || ladderCheck || replayCheck;
    if (seconds > 10 && !longCheck) throw std::runtime_error("--phase-seconds above 10 requires --legacy-check, --bow-check, --zone-check, --ladder-check or --replay-check");
    if (longCheck && (markPositions || markerTrial)) throw std::runtime_error("--legacy-check, --bow-check, --zone-check, --ladder-check and --replay-check do not use position markers");
    if (markerTrial && !positionCheck) throw std::runtime_error("--marker-trial requires --position-check at fixed force 8");
    if (!plan && (positionCheck || markPositions || waitReady) && (!activate || singleEffect.empty())) {
        throw std::runtime_error("Position checks, markers and readiness require --activate and --single-effect");
    }
    std::span<const Phase> selectedPhases = vibrationCheck
        ? std::span<const Phase>(vibrationPhases) : std::span<const Phase>(phases);
    if (positionCheck) selectedPhases = positionPhases;
    if (legacyCheck) selectedPhases = legacyPhases;
    if (bowCheck) selectedPhases = bowPhases;
    if (zoneCheck) selectedPhases = zonePhases;
    if (replayCheck) selectedPhases = replayPhases;
    std::vector<Phase> zoneFrequencyTable;
    if (zoneCheck && zoneVibrationFrequency) {
        zoneFrequencyTable.assign(zonePhases.begin(), zonePhases.end());
        for (auto& phase : zoneFrequencyTable) {
            if (phase.native[0] == 0x26) phase.native[9] = *zoneVibrationFrequency;
        }
        selectedPhases = zoneFrequencyTable;
    }
    std::vector<std::string> ladderNames;
    std::vector<Phase> ladderTable;
    if (ladderCheck) selectedPhases = ladderPhases;
    if (ladderCheck && ladderForces) {
        ladderNames.reserve(ladderForces->size());
        ladderTable.reserve(ladderForces->size() * 2 + 2);
        ladderTable.push_back({"normal", {5}});
        for (const unsigned force : *ladderForces) {
            ladderNames.push_back("race_force_" + std::to_string(force));
            ladderTable.push_back({ladderNames.back(), {0x01, 0, static_cast<std::uint8_t>(force)}});
            ladderTable.push_back({"gap", {5}});
        }
        ladderTable.push_back({"normal", {5}});
        selectedPhases = ladderTable;
    }
    std::array<Phase, 3> isolatedPhases{};
    if (!singleEffect.empty()) {
        const auto found = std::find_if(selectedPhases.begin(), selectedPhases.end(),
            [&](const Phase& phase) { return phase.name == singleEffect && phase.name != "normal" && phase.name != "gap"; });
        if (found == selectedPhases.end()) throw std::runtime_error("Unknown active phase for --single-effect");
        isolatedPhases = {phases.front(), *found, phases.front()};
        selectedPhases = isolatedPhases;
    }
    if (snapTrial && (singleEffect.empty() ||
        asb::dualsense::translateAdaptiveTrigger(asb::TriggerSide::Right, selectedPhases[1].native, 0)->mode != asb::TriggerMode::SniperBreak)) {
        throw std::runtime_error("--snap-trial requires a single weapon phase");
    }
    const auto staticCommandFor = [&](const Phase& phase) {
        auto staticCommand = asb::dualsense::translateAdaptiveTrigger(asb::TriggerSide::Right, phase.native, 0);
        if (zoneCheck && staticCommand) *staticCommand = scaleZoneStaticCommand(*staticCommand, zoneUnits);
        return staticCommand;
    };
    const auto phaseSecondsFor = [&](const Phase& phase, const asb::ForceTriggerCommand& command) {
        if (ladderCheck) {
            if (phase.name == "gap") return kLadderGapSeconds;
            return command.mode == asb::TriggerMode::Normal ? (std::min)(seconds, kLadderNormalMaxSeconds) : seconds;
        }
        return longCheck && command.mode == asb::TriggerMode::Normal ? (std::min)(seconds, 6U) : seconds;
    };
    if (plan) {
        for (const auto& phase : selectedPhases) {
            const auto command = staticCommandFor(phase);
            std::cout << phase.name << " mode=" << static_cast<unsigned>(command->mode) << " params=";
            writeBytes(std::cout, command->params);
            if (ladderCheck) std::cout << " duration=" << phaseSecondsFor(phase, *command) << 's';
            std::cout << '\n';
            if (const auto bow = asb::dualsense::describeBowEmulation(asb::TriggerSide::Right, phase.native);
                bow && phase.dynamic) {
                std::cout << "  bridge bow: draw mode=" << static_cast<unsigned>(bow->draw.mode) << " params=";
                writeBytes(std::cout, bow->draw.params);
                std::cout << ", rt>=" << static_cast<unsigned>(bow->ruptureAxis) << ": mode="
                          << static_cast<unsigned>(bow->rupture.mode) << " params=";
                writeBytes(std::cout, bow->rupture.params);
                std::cout << ", rt<=" << static_cast<unsigned>(bow->rearmAxis) << ": draw again\n";
            }
            for (const auto& manual : replayManualBows) {
                if (!replayCheck || manual.phase != phase.name) continue;
                std::cout << "  manual 1.0.2 bow: rt>=" << manual.switchAxis << ": mode="
                          << static_cast<unsigned>(manual.rupture.mode) << " params=";
                writeBytes(std::cout, manual.rupture.params);
                std::cout << ", rt<=" << manual.rearmAxis << ": draw again\n";
            }
            if (const auto emulation = asb::dualsense::describeZoneEmulation(phase.native)) {
                for (unsigned zone = 0; zone < 10; ++zone) {
                    const auto zoneCommand = asb::dualsense::renderZoneCommand(
                        asb::TriggerSide::Right, *emulation, zone, zoneUnits);
                    std::cout << "  zone " << zone << ": mode=" << static_cast<unsigned>(zoneCommand.mode) << " params=";
                    writeBytes(std::cout, zoneCommand.params);
                    std::cout << '\n';
                }
            }
            if (const auto periodic = asb::dualsense::describePeriodicEmulation(phase.native)) {
                const double horizon = periodic->kind == asb::dualsense::PeriodicEmulation::Kind::Galloping
                    ? 2.0 / periodic->frequency : 2.0 * periodic->periodTenths / 10.0;
                std::optional<asb::ForceTriggerCommand> previous;
                for (double seconds = 0.0; seconds < horizon; seconds += 0.005) {
                    const auto command = asb::dualsense::renderPeriodicCommand(
                        asb::TriggerSide::Right, *periodic, seconds, true, zoneUnits);
                    if (previous && previous->mode == command.mode &&
                        std::abs(static_cast<int>(previous->params[2]) - static_cast<int>(command.params[2])) < 10) {
                        continue;
                    }
                    previous = command;
                    std::cout << "  t=" << seconds << "s: mode=" << static_cast<unsigned>(command.mode) << " params=";
                    writeBytes(std::cout, command.params);
                    std::cout << '\n';
                }
            }
        }
        return 0;
    }
    if (outputPath.empty() || std::filesystem::exists(outputPath)) {
        std::cerr << "A new --output path is required; existing captures are never overwritten.\n";
        return 1;
    }
    std::ofstream output(outputPath, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot create capture file");
    std::string error;
    auto owner = asb::platform::createGlobalSessionStop(error);
    if (!owner) throw std::runtime_error(error);
    const auto candidates = asb::flydigi::Apex5Device::findCandidates(error);
    if (candidates.size() != 1) throw std::runtime_error("Exactly one APEX candidate is required. " + error);
    const auto started = Clock::now();
    std::vector<WriteRecord> writes;
    writes.reserve(64);
    asb::flydigi::TransportPtr transport(asb::platform::createHidTransport(candidates.front(), error));
    if (!transport) throw std::runtime_error(error);
    asb::flydigi::Apex5Device device(asb::flydigi::TransportPtr(
        new RecordingTransport(std::move(transport), started, writes)));
    if (!device.verifyIdentity(error) || !device.identity()->supportsAdaptiveTriggers()) {
        throw std::runtime_error("Verified APEX 4/5 required. " + error);
    }
    if (zoneCheck && device.identity()->isApex4()) {
        throw std::runtime_error("--zone-check requires a verified APEX 5; zone emulation is not available on an APEX 4");
    }
    if (asb::platform::connectedXInputGamepads().size() != 1) {
        throw std::runtime_error("Exactly one XInput device is required for unambiguous axis capture");
    }
    auto input = asb::platform::openXInputGamepadForDevice(
        device.info().vendorId, device.info().productId, std::nullopt, error);
    if (!input) throw std::runtime_error(error);
    asb::dualsense::DualSenseInputState state{};
    if (!input->poll(state, error)) throw std::runtime_error(error);
    output << "{\"kind\":\"metadata\",\"schema\":1,\"model\":\""
           << asb::cli::jsonEscape(device.identity()->describe())
           << "\",\"device_type\":" << static_cast<unsigned>(device.identity()->deviceType())
           << ",\"connection_raw\":" << static_cast<unsigned>(device.identity()->connectionTypeRaw())
           << ",\"firmware_raw\":" << device.identity()->firmwareVersion()
           << ",\"side\":\"rt\",\"activate\":" << (activate ? "true" : "false")
           << ",\"phase_seconds\":" << seconds << ",\"axis_source\":\"xinput_mapped\""
           << ",\"mark_positions\":" << (markPositions ? "true" : "false")
           << ",\"marker_trial\":" << (markerTrial ? "true" : "false")
           << ",\"active_trial_max_seconds\":" << (markerTrial ? 30U : seconds)
           << ",\"snap_trial\":" << (snapTrial ? "true" : "false")
           << ",\"wait_ready\":" << (waitReady ? "true" : "false")
           << ",\"zone_check\":" << (zoneCheck ? "true" : "false")
           << ",\"replay_check\":" << (replayCheck ? "true" : "false")
           << ",\"zone_force_unit\":" << static_cast<unsigned>(zoneUnits.force)
           << ",\"zone_vibration_unit\":" << static_cast<unsigned>(zoneUnits.vibration)
           << ",\"zone_force_law\":\""
           << (zoneForceTableGiven ? "custom" : (perceptualForceLaw ? "perceptual" : "linear")) << '"'
           << ",\"zone_force_table\":";
    writeBytes(output, zoneUnits.forceTable);
    output << ",\"gallop_frequency\":" << static_cast<unsigned>(zoneUnits.gallopingCarrier)
           << ",\"gallop_amplitude\":" << static_cast<unsigned>(zoneUnits.gallopingAmplitude)
           << ",\"gallop_beat_slots\":" << zoneUnits.gallopingBeatSlots
           << ",\"gallop_min_beat_seconds\":" << zoneUnits.gallopingMinimumBeatSeconds
           << ",\"machine_floor\":" << static_cast<unsigned>(zoneUnits.machineAmplitudeFloor)
           << ",\"zone_vibration_frequency\":" << (zoneVibrationFrequency ? static_cast<unsigned>(*zoneVibrationFrequency) : 0U)
           << ",\"zone_vibration_table\":";
    writeBytes(output, zoneUnits.vibrationTable);
    output << ",\"ladder_check\":" << (ladderCheck ? "true" : "false") << ",\"ladder_forces\":";
    if (!ladderCheck) writeBytes(output, std::array<unsigned, 0>{});
    else if (ladderForces) writeBytes(output, *ladderForces);
    else writeBytes(output, kDefaultLadderForces);
    output << "}\n";
    output.flush();
    std::unique_ptr<asb::TriggerResetGuard> reset;
    AsyncWriteStop asyncWriteStop{device};
    std::unique_ptr<asb::dualsense::AdaptiveTriggerBridge> bridge;
    std::uint64_t zoneProfiles = 0;
    std::uint64_t zoneTransitions = 0;
    std::uint64_t periodicProfiles = 0;
    std::uint64_t periodicTransitions = 0;
    if (activate) {
        reset = std::make_unique<asb::TriggerResetGuard>(device);
        if (!device.clearAll(error) || !device.stopRumble(error)) throw std::runtime_error(error);
    }
    std::vector<Sample> samples;
    samples.reserve(11000);
    std::vector<PhaseRecord> phaseRecords;
    phaseRecords.reserve(selectedPhases.size());
    std::vector<PositionMarker> positionMarkers;
    positionMarkers.reserve(64);
    SetConsoleCtrlHandler(consoleHandler, TRUE);
    bool success = true;
    const auto confirmReadiness = [&]() {
        std::cout << "Neutral pause: release RT, then press and release Y to start the active effect immediately (60-second timeout). X cancels.\n" << std::flush;
        const auto deadline = Clock::now() + std::chrono::seconds(60);
        bool readyPressed = false;
        bool ready = false;
        bool previousHeld = (state.buttons & asb::dualsense::button::kTriangle) != 0;
        while (Clock::now() < deadline && !stopRequested.load() && !owner->stopRequested()) {
            if (!input->poll(state, error)) { success = false; break; }
            if ((state.buttons & asb::dualsense::button::kSquare) != 0) {
                stopRequested.store(true);
                break;
            }
            const bool held = (state.buttons & asb::dualsense::button::kTriangle) != 0;
            if (held && !previousHeld && state.r2 <= 5 && state.l2 <= 5) readyPressed = true;
            if (readyPressed && !held && state.r2 <= 5 && state.l2 <= 5) { ready = true; break; }
            if (state.r2 > 5 || state.l2 > 5) readyPressed = false;
            previousHeld = held;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!ready) {
            success = false;
            if (error.empty()) error = "Readiness not confirmed; no active effect sent";
        }
        return ready;
    };
    if (success) std::cout << "Verified " << device.identity()->describe() << ". Capture starts in 3 seconds.\n"
              << "Slowly press/release RT repeatedly; do not press LT. Release RT if uncomfortable; Ctrl+C stops the capture.\n" << std::flush;
    if (success && activate && ladderCheck) {
        std::cout << "Ladder: keep pressing RT slowly during every step; the trigger is free during "
                  << kLadderGapSeconds << "-second gaps.\n" << std::flush;
    }
    if (snapTrial) {
        std::cout << "Active phase: A marks felt blockage; B marks position after the blockage snaps. X cancels.\n" << std::flush;
    } else if (markPositions) {
        std::cout << "Active phase only: hold RT at perceived onset and tap A; at rupture tap B. X cancels.\n" << std::flush;
    }
    if (snapTrial) std::cout << "Active trial ends at B after A, with a 30-second maximum. Do not mark the beginning of your movement as blockage.\n" << std::flush;
    else if (markerTrial) std::cout << "Active trial ends at A (Race) or B after A (weapon), with a 30-second maximum. Do not mark the beginning of your movement as onset.\n" << std::flush;
    if (success) std::this_thread::sleep_for(std::chrono::seconds(3));
    const auto endDynamicPhase = [&]() {
        if (!bridge) return;
        bridge->quiesce();
        device.stopAsyncWrites();
        const auto stats = bridge->stats();
        zoneProfiles += stats.zoneProfiles;
        zoneTransitions += stats.zoneTransitions;
        periodicProfiles += stats.periodicProfiles;
        periodicTransitions += stats.periodicTransitions;
        std::cout << "Zone emulation: profiles=" << stats.zoneProfiles
                  << " transitions=" << stats.zoneTransitions
                  << "; periodic profiles=" << stats.periodicProfiles
                  << " transitions=" << stats.periodicTransitions << '\n'
                  << "Latency (microseconds): zone update avg="
                  << (stats.zoneLatencySamples != 0 ? stats.zoneLatencyTotalUs / stats.zoneLatencySamples : 0)
                  << " max=" << stats.zoneLatencyMaxUs
                  << "; periodic tick lateness avg="
                  << (stats.periodicLatenessSamples != 0
                          ? stats.periodicLatenessTotalUs / stats.periodicLatenessSamples : 0)
                  << " max=" << stats.periodicLatenessMaxUs << '\n' << std::flush;
        std::string asyncError;
        if (success && bridge->failed()) {
            success = false;
            error = bridge->error();
        } else if (success && device.takeAsyncWriteError(asyncError)) {
            success = false;
            error = asyncError;
        }
        bridge.reset();
    };
    asb::tools::TriggerPositionMarkers markerEdges(state.buttons);
    const unsigned phaseCount = activate ? static_cast<unsigned>(selectedPhases.size()) : 1U;
    for (unsigned phaseIndex = 0; success && phaseIndex < phaseCount; ++phaseIndex) {
        if (stopRequested.load() || owner->stopRequested()) break;
        const auto& phase = selectedPhases[phaseIndex];
        const auto command = staticCommandFor(phase);
        if (waitReady && command->mode != asb::TriggerMode::Normal) {
            if (!confirmReadiness()) break;
            markerEdges.update(state.buttons, false);
        }
        phaseRecords.push_back({elapsedUs(started), phaseIndex, *command});
        std::cout << "PHASE " << phaseIndex << ' ' << phase.name << '\n' << std::flush;
        PulseLadderRunner pulseLadder;
        if (activate && zoneCheck && phase.name == "pulse_ladder") {
            pulseLadder.start(device, zoneUnits, pulseKick);
        } else if (activate && phase.dynamic) {
            if (!device.startAsyncWrites(error)) { success = false; break; }
            bridge = std::make_unique<asb::dualsense::AdaptiveTriggerBridge>(device, 100U, true);
            bridge->setZoneUnits(zoneUnits);
            bridge->updateTriggerPositions(state.l2, state.r2);
            asb::dualsense::DualSenseFeedback feedback;
            feedback.kind = asb::dualsense::FeedbackKind::HidOutput;
            feedback.enableBits1 = 0x04;
            feedback.rightTriggerEffect = phase.native;
            bridge->handle(feedback);
            if (bridge->failed()) {
                success = false;
                error = bridge->error();
                break;
            }
        } else if (activate && !device.setTriggerRaw(*command, error)) { success = false; break; }
        const bool activeTrial = markerTrial && command->mode != asb::TriggerMode::Normal;
        asb::tools::MarkerTrialProgress trialProgress(command->mode == asb::TriggerMode::SniperBreak);
        const auto manualBow = std::find_if(replayManualBows.begin(), replayManualBows.end(),
            [&](const ManualBow& bow) { return bow.phase == phase.name; });
        const bool replayBow = replayCheck && activate && manualBow != replayManualBows.end();
        const bool emulatedBow = (bowCheck && activate && phase.name == "bow_emulated_draw4_race_then_break") || replayBow;
        const asb::ForceTriggerCommand breakCommand = replayBow
            ? manualBow->rupture
            : asb::ForceTriggerCommand{asb::TriggerSide::Right, asb::TriggerMode::SniperBreak, {115, 40, 32, 0, 0}};
        const unsigned switchAxis = replayBow ? manualBow->switchAxis : bowSwitchAxis;
        const unsigned rearmAxis = replayBow ? manualBow->rearmAxis : 40U;
        bool emulatedBroken = false;
        const unsigned phaseSeconds = phaseSecondsFor(phase, *command);
        const auto deadline = Clock::now() + std::chrono::seconds(activeTrial ? 30U : phaseSeconds);
        while (Clock::now() < deadline && !stopRequested.load() && !owner->stopRequested()) {
            if (!input->poll(state, error)) { success = false; break; }
            const auto sampledAt = elapsedUs(started);
            samples.push_back({sampledAt, phaseIndex, state.l2, state.r2, state.buttons});
            if (bridge) {
                bridge->updateTriggerPositions(state.l2, state.r2);
                std::string asyncError;
                if (bridge->failed()) {
                    success = false;
                    error = bridge->error();
                    break;
                }
                if (device.takeAsyncWriteError(asyncError)) {
                    success = false;
                    error = asyncError;
                    break;
                }
            }
            if (emulatedBow) {
                if (!emulatedBroken && state.r2 >= switchAxis) {
                    if (!device.setTriggerRaw(breakCommand, error)) { success = false; break; }
                    emulatedBroken = true;
                } else if (emulatedBroken && state.r2 <= rearmAxis) {
                    if (!device.setTriggerRaw(*command, error)) { success = false; break; }
                    emulatedBroken = false;
                }
            }
            const auto edges = markerEdges.update(state.buttons, activate && command->mode != asb::TriggerMode::Normal);
            if (markPositions && edges.onset) positionMarkers.push_back({sampledAt, phaseIndex, state.r2, snapTrial ? "block" : "onset", "A"});
            if (markPositions && edges.release) positionMarkers.push_back({sampledAt, phaseIndex, state.r2, snapTrial ? "after_break" : "release", "B"});
            if ((markPositions || waitReady) && (state.buttons & asb::dualsense::button::kSquare) != 0) {
                stopRequested.store(true);
            }
            if (activeTrial && trialProgress.update(edges)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pulseLadder.join();
        if (success && pulseLadder.failed) {
            success = false;
            error = pulseLadder.error;
        }
        endDynamicPhase();
        if (!success) break;
    }
    endDynamicPhase();
    bool neutralized = false;
    if (activate) {
        std::string resetError;
        neutralized = device.clearAll(resetError);
        if (neutralized) reset->dismiss();
        else { success = false; error += " Reset: " + resetError; }
    }
    for (const auto& record : phaseRecords) {
        output << "{\"kind\":\"phase\",\"elapsed_us\":" << record.elapsed
               << ",\"phase\":" << record.phase << ",\"name\":\"" << selectedPhases[record.phase].name
               << "\",\"dynamic\":" << (selectedPhases[record.phase].dynamic ? "true" : "false")
               << ",\"synthetic_native\":";
        writeBytes(output, selectedPhases[record.phase].native);
        output << ",\"mode\":" << static_cast<unsigned>(record.command.mode) << ",\"params\":";
        writeBytes(output, record.command.params);
        output << "}\n";
    }
    for (const auto& record : writes) {
        output << "{\"kind\":\"hid_write\",\"begin_us\":" << record.beginUs
               << ",\"end_us\":" << record.endUs << ",\"success\":" << (record.success ? "true" : "false")
               << ",\"bytes\":";
        writeBytes(output, record.bytes);
        output << ",\"error\":\"" << asb::cli::jsonEscape(record.error) << "\"}\n";
    }
    for (const auto& sample : samples) {
        output << "{\"kind\":\"axis\",\"elapsed_us\":" << sample.elapsed
               << ",\"phase\":" << sample.phase << ",\"lt\":" << static_cast<unsigned>(sample.left)
               << ",\"rt\":" << static_cast<unsigned>(sample.right) << ",\"buttons\":" << sample.buttons << "}\n";
    }
    for (const auto& marker : positionMarkers) {
        output << "{\"kind\":\"position_marker\",\"elapsed_us\":" << marker.elapsed
               << ",\"phase\":" << marker.phase << ",\"rt\":" << static_cast<unsigned>(marker.right)
               << ",\"label\":\"" << marker.label << "\",\"button\":\"" << marker.button
               << "\",\"source\":\"human_button\"}\n";
    }
    output << "{\"kind\":\"end\",\"success\":" << (success ? "true" : "false")
           << ",\"interrupted\":" << ((stopRequested.load() || owner->stopRequested()) ? "true" : "false")
           << ",\"neutralized\":" << (!activate ? "null" : (neutralized ? "true" : "false"))
           << ",\"samples\":" << samples.size() << ",\"zone_profiles\":" << zoneProfiles
           << ",\"zone_transitions\":" << zoneTransitions << ",\"periodic_profiles\":" << periodicProfiles
           << ",\"periodic_transitions\":" << periodicTransitions
           << ",\"error\":\"" << asb::cli::jsonEscape(error) << "\"}\n";
    output.flush();
    if (!output) throw std::runtime_error("Capture file write failed");
    std::cout << "Capture saved: " << outputPath.string() << "; samples=" << samples.size()
              << "; neutralized=" << neutralized << '\n';
    if (ladderCheck && activate) {
        std::cout << "Ladder summary (RT axis samples per active step):\n"
                  << std::format("{:<16} {:>6} {:>8} {:>6} {:>8}\n", "phase", "force", "samples", "max_rt", "mean_rt");
        for (const auto& record : phaseRecords) {
            if (record.command.mode == asb::TriggerMode::Normal) continue;
            std::uint64_t count = 0;
            std::uint64_t sum = 0;
            unsigned maxRight = 0;
            for (const auto& sample : samples) {
                if (sample.phase != record.phase) continue;
                ++count;
                sum += sample.right;
                maxRight = (std::max)(maxRight, static_cast<unsigned>(sample.right));
            }
            const double mean = count == 0 ? 0.0 : static_cast<double>(sum) / static_cast<double>(count);
            std::cout << std::format("{:<16} {:>6} {:>8} {:>6} {:>8.1f}\n", selectedPhases[record.phase].name,
                                     static_cast<unsigned>(record.command.params[1]), count, maxRight, mean);
        }
        std::cout << std::flush;
    }
    if (!success) std::cerr << error << '\n';
    return success ? 0 : 2;
}

}

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
