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
    telemetry.physicalStats.triggerSourceSwitches = 23;
    telemetry.physicalStats.vendorMaximumGapMs = 64;
    telemetry.physicalStats.activity.r2Oscillations = 24;
    telemetry.physicalStats.activity.bouncedButtons = 0x0420;
    telemetry.inputBackend = "hid\\vendor";
    telemetry.independentTriggerStartupRecoveries = 1;
    telemetry.independentTriggerRuntimeRecoveries = 1;
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
    assert(json.find("\"independent_trigger_startup_recoveries\": 1") != std::string::npos);
    assert(json.find("\"independent_trigger_runtime_recoveries\": 1") != std::string::npos);
    assert(json.find("\"physical_trigger_source_switches\": 23,") != std::string::npos);
    assert(json.find("\"physical_vendor_max_gap_ms\": 64,") != std::string::npos);
    assert(json.find("\"physical_r2_oscillations\": 24,") != std::string::npos);
    assert(json.find("\"physical_bounced_buttons\": 1056,") != std::string::npos);

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
        "physical_vendor_max_gap_ms", "physical_vendor_stale_trigger_gaps",
        "physical_trigger_source_switches", "physical_changes_sticks",
        "physical_changes_l2", "physical_changes_r2", "physical_changes_buttons",
        "physical_changes_dpad", "physical_changes_imu", "physical_l2_oscillations",
        "physical_r2_oscillations", "physical_button_bounces",
        "physical_trigger_button_bounces", "physical_bounced_buttons",
        "physical_bounced_dpad", "forwarded_physical_reports", "keepalive_reports", "lost_reports",
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
    telemetry.apex6AudioFormat = asb::platform::HapticAudioFormat{
        asb::platform::HapticAudioFormatStatus::Quadraphonic, 4, 0x33, 0x33};
    telemetry.apex6Stats->bowBreaks = 3;
    telemetry.apex6Stats->pcmGainPercent = 150;
    telemetry.apex6Stats->pcmOutputLeftPeak = 48;
    telemetry.apex6Stats->pcmOutputRightPeak = 47;
    telemetry.apex6Stats->pcmMode = asb::dualsense::Apex6PcmMode::Normalize;
    telemetry.apex6Stats->pcmReferencePercent = 30;
    telemetry.apex6Stats->pcmReferencePeakAbs16 = 9830;
    telemetry.apex6Stats->pcmTargetPercent = 90;
    telemetry.apex6Stats->pcmRequestedGainRatio = 3.0;
    telemetry.apex6Stats->pcmEffectiveGainRatio = 3.0;
    telemetry.apex6Stats->pcmGainLimited = false;
    telemetry.apex6Stats->pcmMeasurementStatus = "valid";
    telemetry.apex6Stats->pcmOutputClippedSamples = 5;
    telemetry.apex6Stats->pcmProcessedSamples = 800;
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
    assert(apex6Output.str().find("\"audio_format\": \"quadraphonic\"") != std::string::npos);
    assert(apex6Output.str().find("\"audio_mix_channels\": 4") != std::string::npos);
    assert(apex6Output.str().find("\"audio_channel_mask\": 51") != std::string::npos);
    assert(apex6Output.str().find("\"bow_breaks\": 3") != std::string::npos);
    assert(apex6Output.str().find("\"pcm_gain_percent\": 150") != std::string::npos);
    assert(apex6Output.str().find("\"haptic_threshold_percent\": 0") != std::string::npos);
    assert(apex6Output.str().find("\"pcm_output_left_peak\": 48") != std::string::npos);
    assert(apex6Output.str().find("\"pcm_output_right_peak\": 47") != std::string::npos);
    assert(apex6Output.str().find("\"pcm_mode\": \"normalize\"") != std::string::npos);
    assert(apex6Output.str().find("\"pcm_reference_percent\": 30") != std::string::npos);
    assert(apex6Output.str().find("\"pcm_reference_peak_abs16\": 9830") != std::string::npos);
    assert(apex6Output.str().find("\"pcm_target_percent\": 90") != std::string::npos);
    assert(apex6Output.str().find("\"pcm_effective_gain_ratio\": 3") != std::string::npos);
    assert(apex6Output.str().find("\"pcm_gain_limited\": false") != std::string::npos);
    assert(apex6Output.str().find("\"pcm_measurement_status\": \"valid\"") != std::string::npos);
    assert(apex6Output.str().find("\"pcm_output_clipped_samples\": 5") != std::string::npos);
    assert(apex6Output.str().find("\"pcm_processed_samples\": 800") != std::string::npos);
    assert(apex6Output.str().find("\"pcm_gain_policy\": \"bounded-companding-opt-in\"") != std::string::npos);
    assert(apex6Output.str().find("\"waveform_threshold_policy\": \"native-pcm-preserved\"") != std::string::npos);
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
