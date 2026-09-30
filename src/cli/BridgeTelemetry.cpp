#include "cli/BridgeTelemetry.h"

#include "cli/JsonSupport.h"

#include <fstream>
#include <iomanip>
#include <ostream>

namespace asb::cli {

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
        output << ",\n  \"apex6\": {\n"
               << "    \"hid_reports\": " << stats.hidReports << ",\n"
               << "    \"trigger_left_updates\": " << stats.triggerLeftUpdates << ",\n"
               << "    \"trigger_right_updates\": " << stats.triggerRightUpdates << ",\n"
               << "    \"trigger_active_updates\": " << stats.triggerActiveUpdates << ",\n"
               << "    \"trigger_stops\": " << stats.triggerStops << ",\n"
               << "    \"trigger_unsupported\": " << stats.triggerUnsupported << ",\n"
               << "    \"weapon_breaks\": " << stats.weaponBreaks << ",\n"
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
               << "    \"haptic_frames\": " << stats.framesWritten << ",\n"
               << "    \"haptic_enables\": " << stats.hapticEnables << ",\n"
               << "    \"haptic_disables\": " << stats.hapticDisables << ",\n"
               << "    \"write_failures\": " << stats.writeFailures << "\n"
               << "  }";
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
