#pragma once

#include "cli/BridgeRuntimeSupport.h"
#include "dualsense/AdaptiveTriggerBridge.h"
#include "dualsense/Apex6HapticBridge.h"
#include "dualsense/VirtualDualSense.h"
#include "platform/PhysicalInputSource.h"
#include "platform/AudioEndpointProtection.h"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>

namespace asb::cli {

struct BridgeTelemetry {
    asb::dualsense::VirtualDualSenseStats virtualStats;
    std::optional<asb::dualsense::Apex6HapticBridgeStats> apex6Stats;
    std::optional<asb::platform::HapticAudioFormat> apex6AudioFormat;
    asb::platform::PhysicalInputSourceStats physicalStats;
    ProcessUsageSnapshot processUsage;
    std::string inputBackend;
    unsigned int independentTriggerStartupRecoveries = 0;
    unsigned int independentTriggerRuntimeRecoveries = 0;
    bool virtualInputMonitorEnabled = false;
    std::size_t startupAttempts = 0;
    std::int64_t initializationMilliseconds = 0;
    std::int64_t physicalInputInitializationMilliseconds = 0;
    std::int64_t virtualInputInitializationMilliseconds = 0;
    std::int64_t firmwareInitializationMilliseconds = 0;
    std::int64_t isolationInitializationMilliseconds = 0;
    std::int64_t runtimeMilliseconds = 0;
    std::uint64_t latencyP50Us = 0;
    std::uint64_t latencyP95Us = 0;
    std::uint64_t latencyP99Us = 0;
    std::uint64_t latencySamples = 0;
    double physicalReportRateHz = 0.0;
    double virtualReportRateHz = 0.0;
    std::uint64_t forwardedPhysicalReports = 0;
    std::uint64_t keepaliveReports = 0;
    std::uint64_t lostReports = 0;
    std::uint64_t coalescedReports = 0;
    std::uint8_t maximumSimultaneousTriggers = 0;
    std::uint64_t simultaneousTriggerReports = 0;
    std::uint8_t virtualMaximumSimultaneousTriggers = 0;
    std::uint64_t virtualSimultaneousTriggerReports = 0;
    std::uint8_t batteryPercent = 0;
    std::uint8_t chargeState = 0;
    double cpuPercent = 0.0;
};

void writeBridgeTelemetry(std::ostream& output, const BridgeTelemetry& telemetry);
void writeAdaptiveTriggerDiagnostics(std::ostream& output,
                                     const asb::dualsense::AdaptiveTriggerBridgeStats& stats);
void writeApex6TriggerTrace(std::ostream& output,
                           const asb::dualsense::Apex6HapticBridgeStats& stats);
[[nodiscard]] bool writeBridgeTelemetryFile(
    const std::filesystem::path& path,
    const BridgeTelemetry& telemetry,
    std::string& error);

} // namespace asb::cli
