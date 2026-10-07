#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "cli/JsonSupport.h"
#include "core/TriggerResetGuard.h"
#include "dualsense/AdaptiveTriggerTranslation.h"
#include "flydigi/Apex5Device.h"
#include "platform/SessionControl.h"
#include "platform/XInputGamepad.h"
#include "tools/TriggerPositionMarkers.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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
    bool activate = false;
    bool plan = false;
    bool vibrationCheck = false;
    bool positionCheck = false;
    bool markPositions = false;
    bool waitReady = false;
    bool markerTrial = false;
    bool snapTrial = false;
    std::string singleEffect;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--activate") activate = true;
        else if (option == "--plan") plan = true;
        else if (option == "--vibration-check") vibrationCheck = true;
        else if (option == "--position-check") positionCheck = true;
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
            if (consumed != value.size() || parsed < 1 || parsed > 10) {
                throw std::runtime_error("--phase-seconds must be 1..10");
            }
            seconds = static_cast<unsigned>(parsed);
        } else {
            std::cerr << "Usage: Apex45TriggerCapture --output FILE [--activate] "
                         "[--vibration-check | --position-check] [--single-effect PHASE_NAME] "
                         "[--mark-positions] [--wait-ready] [--marker-trial | --snap-trial] [--phase-seconds 1..10] [--plan]\n";
            return 1;
        }
    }
    if (positionCheck && vibrationCheck) throw std::runtime_error("Choose only one check family");
    if (markerTrial && !positionCheck) throw std::runtime_error("--marker-trial requires --position-check at fixed force 8");
    if (!plan && (positionCheck || markPositions || waitReady) && (!activate || singleEffect.empty())) {
        throw std::runtime_error("Position checks, markers and readiness require --activate and --single-effect");
    }
    std::span<const Phase> selectedPhases = vibrationCheck
        ? std::span<const Phase>(vibrationPhases) : std::span<const Phase>(phases);
    if (positionCheck) selectedPhases = positionPhases;
    std::array<Phase, 3> isolatedPhases{};
    if (!singleEffect.empty()) {
        const auto found = std::find_if(selectedPhases.begin(), selectedPhases.end(),
            [&](const Phase& phase) { return phase.name == singleEffect && phase.name != "normal"; });
        if (found == selectedPhases.end()) throw std::runtime_error("Unknown active phase for --single-effect");
        isolatedPhases = {phases.front(), *found, phases.front()};
        selectedPhases = isolatedPhases;
    }
    if (snapTrial && (singleEffect.empty() ||
        asb::dualsense::translateAdaptiveTrigger(asb::TriggerSide::Right, selectedPhases[1].native, 0)->mode != asb::TriggerMode::SniperBreak)) {
        throw std::runtime_error("--snap-trial requires a single weapon phase");
    }
    if (plan) {
        for (const auto& phase : selectedPhases) {
            const auto command = asb::dualsense::translateAdaptiveTrigger(asb::TriggerSide::Right, phase.native, 0);
            std::cout << phase.name << " mode=" << static_cast<unsigned>(command->mode) << " params=";
            writeBytes(std::cout, command->params);
            std::cout << '\n';
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
           << ",\"wait_ready\":" << (waitReady ? "true" : "false") << "}\n";
    output.flush();
    std::unique_ptr<asb::TriggerResetGuard> reset;
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
    if (snapTrial) {
        std::cout << "Active phase: A marks felt blockage; B marks position after the blockage snaps. X cancels.\n" << std::flush;
    } else if (markPositions) {
        std::cout << "Active phase only: hold RT at perceived onset and tap A; at rupture tap B. X cancels.\n" << std::flush;
    }
    if (snapTrial) std::cout << "Active trial ends at B after A, with a 30-second maximum. Do not mark the beginning of your movement as blockage.\n" << std::flush;
    else if (markerTrial) std::cout << "Active trial ends at A (Race) or B after A (weapon), with a 30-second maximum. Do not mark the beginning of your movement as onset.\n" << std::flush;
    if (success) std::this_thread::sleep_for(std::chrono::seconds(3));
    asb::tools::TriggerPositionMarkers markerEdges(state.buttons);
    const unsigned phaseCount = activate ? static_cast<unsigned>(selectedPhases.size()) : 1U;
    for (unsigned phaseIndex = 0; success && phaseIndex < phaseCount; ++phaseIndex) {
        if (stopRequested.load() || owner->stopRequested()) break;
        const auto& phase = selectedPhases[phaseIndex];
        const auto command = asb::dualsense::translateAdaptiveTrigger(asb::TriggerSide::Right, phase.native, 0);
        if (waitReady && command->mode != asb::TriggerMode::Normal) {
            if (!confirmReadiness()) break;
            markerEdges.update(state.buttons, false);
        }
        phaseRecords.push_back({elapsedUs(started), phaseIndex, *command});
        std::cout << "PHASE " << phaseIndex << ' ' << phase.name << '\n' << std::flush;
        if (activate && !device.setTriggerRaw(*command, error)) { success = false; break; }
        const bool activeTrial = markerTrial && command->mode != asb::TriggerMode::Normal;
        asb::tools::MarkerTrialProgress trialProgress(command->mode == asb::TriggerMode::SniperBreak);
        const auto deadline = Clock::now() + std::chrono::seconds(activeTrial ? 30U : seconds);
        while (Clock::now() < deadline && !stopRequested.load() && !owner->stopRequested()) {
            if (!input->poll(state, error)) { success = false; break; }
            const auto sampledAt = elapsedUs(started);
            samples.push_back({sampledAt, phaseIndex, state.l2, state.r2, state.buttons});
            const auto edges = markerEdges.update(state.buttons, activate && command->mode != asb::TriggerMode::Normal);
            if (markPositions && edges.onset) positionMarkers.push_back({sampledAt, phaseIndex, state.r2, snapTrial ? "block" : "onset", "A"});
            if (markPositions && edges.release) positionMarkers.push_back({sampledAt, phaseIndex, state.r2, snapTrial ? "after_break" : "release", "B"});
            if ((markPositions || waitReady) && (state.buttons & asb::dualsense::button::kSquare) != 0) {
                stopRequested.store(true);
            }
            if (activeTrial && trialProgress.update(edges)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!success) break;
    }
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
               << "\",\"synthetic_native\":";
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
           << ",\"samples\":" << samples.size() << ",\"error\":\"" << asb::cli::jsonEscape(error) << "\"}\n";
    output.flush();
    if (!output) throw std::runtime_error("Capture file write failed");
    std::cout << "Capture saved: " << outputPath.string() << "; samples=" << samples.size()
              << "; neutralized=" << neutralized << '\n';
    if (!success) std::cerr << error << '\n';
    return success ? 0 : 2;
}

}

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
