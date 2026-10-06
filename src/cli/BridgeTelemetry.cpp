#include "cli/BridgeTelemetry.h"

#include "cli/JsonSupport.h"

#include <fstream>
#include <iomanip>
#include <ostream>

namespace asb::cli {
namespace {

void writeTriggerEntry(std::ostream& output,
                       const asb::dualsense::Apex6TriggerTraceEntry& entry) {
    output << "{\"ms\": " << entry.elapsedMilliseconds
           << ", \"side\": \"" << (entry.side == TriggerSide::Left ? "lt" : "rt")
           << "\", \"status\": \"" << asb::dualsense::apex6TriggerTraceStatusName(entry.status)
           << "\", \"position\": " << static_cast<unsigned>(entry.position)
           << ", \"enable_bits\": " << static_cast<unsigned>(entry.enableBits)
           << ", \"bytes\": [";
    for (std::size_t index = 0; index < entry.bytes.size(); ++index) {
        if (index != 0) output << ',';
        output << static_cast<unsigned>(entry.bytes[index]);
    }
    output << "]}";
}

void writeOptionalTriggerEntry(std::ostream& output,
    const std::optional<asb::dualsense::Apex6TriggerTraceEntry>& entry) {
    if (entry) writeTriggerEntry(output, *entry);
    else output << "null";
}

} // namespace

void writeApex6TriggerTrace(std::ostream& output,
                           const asb::dualsense::Apex6HapticBridgeStats& stats) {
    const auto print = [&output](std::string_view key,
        const std::optional<asb::dualsense::Apex6TriggerTraceEntry>& entry) {
        output << key << '=';
        if (entry) writeTriggerEntry(output, *entry);
        else output << "none";
        output << '\n';
    };
    print("apex6_last_active_lt", stats.lastActiveLeft);
    print("apex6_last_active_rt", stats.lastActiveRight);
    print("apex6_last_rejected_lt", stats.lastRejectedLeft);
    print("apex6_last_rejected_rt", stats.lastRejectedRight);
    output << "apex6_trigger_trace_overwritten=" << stats.triggerTraceOverwritten << '\n';
    for (std::size_t index = 0; index < stats.triggerTraceCount; ++index) {
        output << "apex6_trigger_trace_" << index << '=';
        writeTriggerEntry(output, stats.triggerTrace[index]);
        output << '\n';
    }
}

void writeBridgeTelemetry(std::ostream& output, const BridgeTelemetry& telemetry) {
    const auto& virtualStats = telemetry.virtualStats;
    const auto& physicalStats = telemetry.physicalStats;
    output << std::fixed << std::setprecision(3)
           << "{\n"
           << "  \"schema\": 1,\n"
           << "  \"virtual_backend\": \""
           << jsonEscape(virtualStats.backendVersion) << "\",\n"
           << "  \"input_mode\": \"mandatory-full-proxy\",\n"
           << "  \"input_backend\": \"" << jsonEscape(telemetry.inputBackend) << "\",\n"
           << "  \"independent_trigger_startup_recoveries\": "
           << telemetry.independentTriggerStartupRecoveries << ",\n"
           << "  \"independent_trigger_runtime_recoveries\": "
           << telemetry.independentTriggerRuntimeRecoveries << ",\n"
           << "  \"virtual_input_monitor\": \""
           << (telemetry.virtualInputMonitorEnabled ? "enabled" : "disabled")
           << "\",\n"
           << "  \"virtual_startup_attempts\": " << telemetry.startupAttempts << ",\n"
           << "  \"initialization_ms\": " << telemetry.initializationMilliseconds << ",\n"
           << "  \"initialization_physical_input_ms\": "
           << telemetry.physicalInputInitializationMilliseconds << ",\n"
           << "  \"initialization_virtual_input_ms\": "
           << telemetry.virtualInputInitializationMilliseconds << ",\n"
           << "  \"initialization_firmware_ms\": "
           << telemetry.firmwareInitializationMilliseconds << ",\n"
           << "  \"initialization_isolation_ms\": "
           << telemetry.isolationInitializationMilliseconds << ",\n"
           << "  \"backend_initialization_bootstrap_us\": "
           << virtualStats.initializationBootstrapUs << ",\n"
           << "  \"backend_initialization_server_us\": "
           << virtualStats.initializationServerUs << ",\n"
           << "  \"backend_initialization_bus_us\": "
           << virtualStats.initializationBusUs << ",\n"
           << "  \"backend_initialization_device_us\": "
           << virtualStats.initializationDeviceUs << ",\n"
           << "  \"backend_initialization_feedback_us\": "
           << virtualStats.initializationFeedbackUs << ",\n"
           << "  \"backend_initialization_input_us\": "
           << virtualStats.initializationInputUs << ",\n"
           << "  \"runtime_ms\": " << telemetry.runtimeMilliseconds << ",\n"
           << "  \"forward_latency_us_p50\": " << telemetry.latencyP50Us << ",\n"
           << "  \"forward_latency_us_p95\": " << telemetry.latencyP95Us << ",\n"
           << "  \"forward_latency_us_p99\": " << telemetry.latencyP99Us << ",\n"
           << "  \"forward_latency_samples\": " << telemetry.latencySamples << ",\n"
           << "  \"physical_report_rate_hz\": " << telemetry.physicalReportRateHz << ",\n"
           << "  \"virtual_report_rate_hz\": " << telemetry.virtualReportRateHz << ",\n"
           << "  \"physical_reports\": " << physicalStats.reports << ",\n"
           << "  \"physical_vendor_reports\": " << physicalStats.vendorReports << ",\n"
           << "  \"physical_vendor_states\": " << physicalStats.vendorStates << ",\n"
           << "  \"physical_vendor_parse_failures\": "
           << physicalStats.vendorParseFailures << ",\n"
           << "  \"physical_vendor_read_failures\": "
           << physicalStats.vendorReadFailures << ",\n"
           << "  \"forwarded_physical_reports\": "
           << telemetry.forwardedPhysicalReports << ",\n"
           << "  \"keepalive_reports\": " << telemetry.keepaliveReports << ",\n"
           << "  \"lost_reports\": " << telemetry.lostReports << ",\n"
           << "  \"coalesced_reports\": " << telemetry.coalescedReports << ",\n"
           << "  \"maximum_simultaneous_triggers\": "
           << static_cast<unsigned>(telemetry.maximumSimultaneousTriggers) << ",\n"
           << "  \"simultaneous_trigger_reports\": "
           << telemetry.simultaneousTriggerReports << ",\n"
           << "  \"virtual_maximum_simultaneous_triggers\": "
           << static_cast<unsigned>(telemetry.virtualMaximumSimultaneousTriggers) << ",\n"
           << "  \"virtual_simultaneous_trigger_reports\": "
           << telemetry.virtualSimultaneousTriggerReports << ",\n"
           << "  \"battery_percent\": "
           << static_cast<unsigned>(telemetry.batteryPercent) << ",\n"
           << "  \"charge_state\": " << static_cast<unsigned>(telemetry.chargeState) << ",\n"
           << "  \"cpu_percent_total\": " << telemetry.cpuPercent << ",\n"
           << "  \"working_set_mib\": "
           << static_cast<double>(telemetry.processUsage.workingSetBytes) /
                  (1024.0 * 1024.0) << ",\n"
           << "  \"peak_working_set_mib\": "
           << static_cast<double>(telemetry.processUsage.peakWorkingSetBytes) /
                  (1024.0 * 1024.0) << ",\n"
           << "  \"audio_haptics_received\": " << virtualStats.audioHapticsFrames << ",\n"
           << "  \"dualsense_output_reports\": " << virtualStats.outputReports << ",\n"
           << "  \"dualsense_trigger_reports\": " << virtualStats.triggerReports << ",\n"
           << "  \"dualsense_rumble_reports\": " << virtualStats.rumbleReports << ",\n"
           << "  \"dualsense_malformed_feedback_frames\": "
           << virtualStats.malformedFrames << ",\n"
           << "  \"dualsense_unknown_feedback_frames\": "
           << virtualStats.unknownFrames << ",\n"
           << "  \"audio_haptics_delivered\": " << virtualStats.audioHapticsDelivered << ",\n"
           << "  \"audio_haptics_coalesced\": " << virtualStats.audioHapticsCoalesced;
    if (telemetry.apex6Stats) {
        const auto& stats = *telemetry.apex6Stats;
        const auto audio = telemetry.apex6AudioFormat.value_or(asb::platform::HapticAudioFormat{});
        output << ",\n  \"apex6\": {\n"
               << "    \"audio_format\": \""
               << asb::platform::hapticAudioFormatStatusName(audio.status) << "\",\n"
               << "    \"audio_mix_channels\": " << audio.channels << ",\n"
               << "    \"audio_channel_mask\": " << audio.channelMask << ",\n"
               << "    \"audio_physical_speaker_mask\": " << audio.physicalSpeakerMask << ",\n"
               << "    \"hid_reports\": " << stats.hidReports << ",\n"
               << "    \"trigger_left_updates\": " << stats.triggerLeftUpdates << ",\n"
               << "    \"trigger_right_updates\": " << stats.triggerRightUpdates << ",\n"
               << "    \"trigger_active_updates\": " << stats.triggerActiveUpdates << ",\n"
               << "    \"trigger_stops\": " << stats.triggerStops << ",\n"
               << "    \"trigger_unsupported\": " << stats.triggerUnsupported << ",\n"
               << "    \"trigger_malformed\": " << stats.triggerMalformed << ",\n"
               << "    \"trigger_rejected_stops\": " << stats.triggerRejectedStops << ",\n"
               << "    \"weapon_breaks\": " << stats.weaponBreaks << ",\n"
               << "    \"bow_breaks\": " << stats.bowBreaks << ",\n"
               << "    \"waveform_threshold_policy\": \"native-pcm-preserved\",\n"
               << "    \"pcm_gain_percent\": " << stats.pcmGainPercent << ",\n"
               << "    \"pcm_gain_policy\": \"bounded-companding-opt-in\",\n"
               << "    \"haptic_threshold_percent\": 0,\n"
               << "    \"pcm_output_left_peak\": " << stats.pcmOutputLeftPeak << ",\n"
               << "    \"pcm_output_right_peak\": " << stats.pcmOutputRightPeak << ",\n"
               << "    \"rumble_updates\": " << stats.rumbleUpdates << ",\n"
               << "    \"audio_envelope_reports\": " << stats.audioEnvelopeReports << ",\n"
               << "    \"waveform_blocks\": " << stats.waveformBlocks << ",\n"
               << "    \"waveform_left_active\": " << stats.waveformLeftActiveBlocks << ",\n"
               << "    \"waveform_right_active\": " << stats.waveformRightActiveBlocks << ",\n"
               << "    \"waveform_active_rendered\": " << stats.waveformActiveRendered << ",\n"
               << "    \"waveform_rendered\": " << stats.waveformBlocksRendered << ",\n"
               << "    \"waveform_stale_drops\": " << stats.waveformStaleDrops << ",\n"
               << "    \"waveform_maximum_age_us\": " << stats.waveformMaximumAgeUs << ",\n"
               << "    \"waveform_left_peak\": " << stats.waveformLeftPeak << ",\n"
               << "    \"waveform_right_peak\": " << stats.waveformRightPeak << ",\n"
               << "    \"waveform_silent_blocks\": " << stats.waveformSilentBlocks << ",\n"
               << "    \"waveform_left_thresholded\": " << stats.waveformLeftThresholded << ",\n"
               << "    \"waveform_right_thresholded\": " << stats.waveformRightThresholded << ",\n"
               << "    \"waveform_active_drops\": " << stats.waveformActiveDrops << ",\n"
               << "    \"waveform_left_active_rms\": " << stats.waveformLeftActiveRms << ",\n"
               << "    \"waveform_right_active_rms\": " << stats.waveformRightActiveRms << ",\n"
               << "    \"raw_audio_measured_blocks\": " << stats.rawAudioMeasuredBlocks << ",\n"
               << "    \"raw_audio_frames\": " << stats.rawAudioFrames << ",\n"
               << "    \"raw_speaker_left_peak\": " << stats.rawAudioPeaks[0] << ",\n"
               << "    \"raw_speaker_right_peak\": " << stats.rawAudioPeaks[1] << ",\n"
               << "    \"raw_haptic_left_peak\": " << stats.rawAudioPeaks[2] << ",\n"
               << "    \"raw_haptic_right_peak\": " << stats.rawAudioPeaks[3] << ",\n"
               << "    \"raw_haptic_left_rms\": " << stats.rawHapticLeftRms << ",\n"
               << "    \"raw_haptic_right_rms\": " << stats.rawHapticRightRms << ",\n"
               << "    \"haptic_frames\": " << stats.framesWritten << ",\n"
               << "    \"haptic_enables\": " << stats.hapticEnables << ",\n"
               << "    \"haptic_disables\": " << stats.hapticDisables << ",\n"
               << "    \"write_failures\": " << stats.writeFailures << ",\n"
               << "    \"last_active_lt\": ";
        writeOptionalTriggerEntry(output, stats.lastActiveLeft);
        output << ",\n    \"last_active_rt\": ";
        writeOptionalTriggerEntry(output, stats.lastActiveRight);
        output << ",\n    \"last_rejected_lt\": ";
        writeOptionalTriggerEntry(output, stats.lastRejectedLeft);
        output << ",\n    \"last_rejected_rt\": ";
        writeOptionalTriggerEntry(output, stats.lastRejectedRight);
        output << ",\n    \"trigger_trace_overwritten\": " << stats.triggerTraceOverwritten
               << ",\n    \"trigger_trace\": [";
        for (std::size_t index = 0; index < stats.triggerTraceCount; ++index) {
            if (index != 0) output << ',';
            writeTriggerEntry(output, stats.triggerTrace[index]);
        }
        output << "]\n  }";
    }
    output << "\n}\n";
}

bool writeBridgeTelemetryFile(
    const std::filesystem::path& path,
    const BridgeTelemetry& telemetry,
    std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not create telemetry JSON file: " + path.string();
        return false;
    }
    writeBridgeTelemetry(output, telemetry);
    if (!output) {
        error = "Could not write telemetry JSON file: " + path.string();
        return false;
    }
    error.clear();
    return true;
}

} // namespace asb::cli
