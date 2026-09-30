#ifdef NDEBUG
#undef NDEBUG
#endif

#include "cli/BridgeTelemetry.h"
#include "cli/JsonSupport.h"

#include <array>
#include <cassert>
#include <sstream>
#include <string>
#include <string_view>

int main() {
    assert(asb::cli::jsonEscape("quote\" slash\\ line\n tab\t") ==
           "quote\\\" slash\\\\ line\\n tab\\t");
    const std::string control(1, '\x01');
    assert(asb::cli::jsonEscape(control) == "\\u0001");

    asb::cli::BridgeTelemetry telemetry{};
    telemetry.virtualStats.backendVersion = "integrated \"test\"";
    telemetry.virtualStats.initializationBootstrapUs = 11;
    telemetry.virtualStats.audioHapticsFrames = 71;
    telemetry.virtualStats.audioHapticsDelivered = 72;
    telemetry.virtualStats.audioHapticsCoalesced = 73;
    telemetry.virtualStats.triggerReports = 12;
    telemetry.physicalStats.reports = 21;
    telemetry.physicalStats.vendorReports = 22;
    telemetry.inputBackend = "hid\\vendor";
    telemetry.virtualInputMonitorEnabled = true;
    telemetry.startupAttempts = 2;
    telemetry.runtimeMilliseconds = 3000;
    telemetry.latencyP50Us = 40;
    telemetry.latencyP95Us = 50;
    telemetry.latencyP99Us = 60;
    telemetry.latencySamples = 70;
    telemetry.physicalReportRateHz = 125.5;
    telemetry.virtualReportRateHz = 999.25;
    telemetry.maximumSimultaneousTriggers = 2;
    telemetry.batteryPercent = 83;
    telemetry.chargeState = 1;
    telemetry.cpuPercent = 1.25;
    telemetry.processUsage.workingSetBytes = 3ULL * 1024ULL * 1024ULL;
    telemetry.processUsage.peakWorkingSetBytes = 4ULL * 1024ULL * 1024ULL;

    std::ostringstream output;
    asb::cli::writeBridgeTelemetry(output, telemetry);
    const auto json = output.str();
    assert(json.starts_with("{\n  \"schema\": 1,\n"));
    assert(json.ends_with("  \"audio_haptics_coalesced\": 73\n}\n"));
    assert(json.find("\"virtual_backend\": \"integrated \\\"test\\\"\"") !=
           std::string::npos);
    assert(json.find("\"input_backend\": \"hid\\\\vendor\"") !=
           std::string::npos);
    assert(json.find("\"physical_report_rate_hz\": 125.500") !=
           std::string::npos);
    assert(json.find("\"working_set_mib\": 3.000") != std::string::npos);
    assert(json.find("\"maximum_simultaneous_triggers\": 2") !=
           std::string::npos);
    assert(json.find("\"virtual_input_monitor\": \"enabled\"") !=
           std::string::npos);
    assert(json.find("\"dualsense_trigger_reports\": 12") !=
           std::string::npos);
    assert(json.find(",\n}\n") == std::string::npos);

    constexpr std::array requiredKeys{
        "schema", "virtual_backend", "input_mode", "input_backend",
        "virtual_input_monitor",
        "virtual_startup_attempts", "initialization_ms",
        "initialization_physical_input_ms", "initialization_virtual_input_ms",
        "initialization_firmware_ms", "initialization_isolation_ms",
        "backend_initialization_bootstrap_us", "backend_initialization_server_us",
        "backend_initialization_bus_us", "backend_initialization_device_us",
        "backend_initialization_feedback_us", "backend_initialization_input_us",
        "runtime_ms", "forward_latency_us_p50", "forward_latency_us_p95",
        "forward_latency_us_p99", "forward_latency_samples",
        "physical_report_rate_hz", "virtual_report_rate_hz", "physical_reports",
        "physical_vendor_reports", "physical_vendor_states",
        "physical_vendor_parse_failures", "physical_vendor_read_failures",
        "forwarded_physical_reports", "keepalive_reports", "lost_reports",
        "coalesced_reports", "maximum_simultaneous_triggers",
        "simultaneous_trigger_reports", "virtual_maximum_simultaneous_triggers",
        "virtual_simultaneous_trigger_reports", "battery_percent", "charge_state",
        "cpu_percent_total", "working_set_mib", "peak_working_set_mib",
        "audio_haptics_received", "dualsense_output_reports",
        "dualsense_trigger_reports", "dualsense_rumble_reports",
        "dualsense_malformed_feedback_frames", "dualsense_unknown_feedback_frames",
        "audio_haptics_delivered",
        "audio_haptics_coalesced"};
    for (const std::string_view key : requiredKeys) {
        const std::string token = "\"" + std::string(key) + "\":";
        const auto first = json.find(token);
        assert(first != std::string::npos);
        assert(json.find(token, first + token.size()) == std::string::npos);
    }

    telemetry.apex6Stats = asb::dualsense::Apex6HapticBridgeStats{};
    telemetry.apex6Stats->hidReports = 17;
    telemetry.apex6Stats->waveformLeftActiveBlocks = 9;
    telemetry.apex6Stats->triggerMalformed = 2;
    telemetry.apex6Stats->rawAudioMeasuredBlocks = 1;
    telemetry.apex6Stats->rawAudioPeaks = {1234, 5678, 10000, 32768};
    asb::dualsense::Apex6TriggerTraceEntry entry{};
    entry.elapsedMilliseconds = 42;
    entry.status = asb::dualsense::Apex6TriggerTraceStatus::Active;
    entry.bytes = {0x26, 0xFF, 0x03, 0, 0, 0, 0, 0, 0, 150, 0};
    telemetry.apex6Stats->lastActiveLeft = entry;
    telemetry.apex6Stats->triggerTrace[0] = entry;
    telemetry.apex6Stats->triggerTraceCount = 1;
    std::ostringstream apex6Output;
    asb::cli::writeBridgeTelemetry(apex6Output, telemetry);
    assert(apex6Output.str().find("\"apex6\": {") != std::string::npos);
    assert(apex6Output.str().find("\"hid_reports\": 17") != std::string::npos);
    assert(apex6Output.str().find("\"waveform_left_active\": 9") != std::string::npos);
    assert(apex6Output.str().find("\"trigger_malformed\": 2") != std::string::npos);
    assert(apex6Output.str().find("\"raw_audio_measured_blocks\": 1") != std::string::npos);
    assert(apex6Output.str().find("\"raw_haptic_right_peak\": 32768") != std::string::npos);
    assert(apex6Output.str().find("\"bytes\": [38,255,3,0,0,0,0,0,0,150,0]") != std::string::npos);
    assert(apex6Output.str().find("\"last_rejected_lt\": null") != std::string::npos);
    assert(apex6Output.str().find(",\n}\n") == std::string::npos);
    std::ostringstream traceOutput;
    asb::cli::writeApex6TriggerTrace(traceOutput, *telemetry.apex6Stats);
    assert(traceOutput.str().find("apex6_trigger_trace_0={\"ms\": 42") != std::string::npos);
    assert(traceOutput.str().find("apex6_last_rejected_lt=none") != std::string::npos);

    return 0;
}
