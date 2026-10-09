#include "cli/Commands.h"
#include "cli/BridgeOptions.h"
#include "cli/BridgeRuntimeSupport.h"
#include "cli/BridgeTelemetry.h"
#include "cli/CommandSupport.h"
#include "core/ApexProfileRestoreGuard.h"
#include "core/Apex4GyroValidation.h"
#include "core/TriggerResetGuard.h"
#include "core/RumbleResetGuard.h"
#include "diagnostics/HidDiagnostics.h"
#include "dualsense/DualSenseFirmware.h"
#include "dualsense/VirtualDualSense.h"
#include "dualsense/VirtualDualSenseStartup.h"
#include "dualsense/AdaptiveTriggerBridge.h"
#include "dualsense/AdaptiveTriggerTranslation.h"
#include "dualsense/Apex6HapticBridge.h"
#include "dualsense/RumbleBridge.h"
#include "dualsense/LightbarBridge.h"
#include "dualsense/TouchpadGestureProfile.h"
#include "flydigi/Apex4Input.h"
#include "flydigi/Apex4Protocol.h"
#include "flydigi/Apex5Device.h"
#include "flydigi/Apex5Protocol.h"
#include "platform/HidTransport.h"
#include "platform/AudioEndpointProtection.h"
#include "platform/PhysicalControllerIsolation.h"
#include "platform/PhysicalInputFreshnessWatchdog.h"
#include "platform/PhysicalInputSource.h"
#include "platform/SessionControl.h"
#include "platform/XInputGamepad.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace asb::cli {

int commandBridgeTriggers(int argc, char** argv) {
    const auto initializationStartedAt = std::chrono::steady_clock::now();
    g_stopRequested.store(false, std::memory_order_relaxed);
    BridgeCommandOptions options{};
    std::string error;
    if (!parseBridgeOptions(argc, argv, options, error)) {
        std::cerr << error << '\n' << bridgeCommandUsage() << '\n';
        return 1;
    }

    auto globalSessionStop = asb::platform::createGlobalSessionStop(error);
    if (!globalSessionStop) {
        std::cerr << "Bridge session ownership failed: " << error << '\n';
        return 13;
    }

    // A controller can go to sleep while a previous session is isolated. The
    // watchdog normally restores HidHide immediately, but run the idempotent
    // recovery here as well before enumerating a freshly-woken controller.
    // Pending profile/transport restoration may still need the controller to
    // finish waking; activate() retries that recovery once identity is back.
    bool recoveredPreviousIsolation = false;
    std::string previousIsolationError;
    if (!asb::platform::TemporaryPhysicalControllerIsolation::recoverPending(
            recoveredPreviousIsolation, previousIsolationError)) {
        std::cerr << "Warning: pending controller recovery is incomplete: "
                  << previousIsolationError << '\n';
    }

    std::unique_ptr<asb::platform::SessionControl> sessionControl;
    if (options.sessionToken) {
        sessionControl = asb::platform::connectSessionControl(
            *options.sessionToken, options.sessionOwnerProcessId, error);
        if (!sessionControl) {
            std::cerr << "Bridge session IPC connection failed: " << error << '\n';
            return 13;
        }
        if (!sessionControl->publish(asb::platform::SessionPhase::Starting, 0,
                                     "Bridge initialization started.", error)) {
            std::string ignored;
            (void)sessionControl->signalReady(ignored);
            std::cerr << "Bridge session status initialization failed: " << error << '\n';
            return 13;
        }
    }
    const auto failSession = [&sessionControl](int exitCode, std::string_view message) {
        if (sessionControl) {
            std::string ignored;
            (void)sessionControl->publish(asb::platform::SessionPhase::Failed,
                                          exitCode, message, ignored);
            // Ready doubles as initialization-complete: on failure it wakes the
            // caller so it can read the status immediately instead of timing out.
            (void)sessionControl->signalReady(ignored);
        }
        return exitCode;
    };

    // Windows publishes the APEX container, mapped gamepad and vendor HID
    // collections independently after wake. Give the vendor interface a
    // bounded window to appear instead of failing on the first empty scan.
    std::optional<asb::flydigi::Apex5Device> device;
    const auto deviceOpenDeadline = std::chrono::steady_clock::now() +
                                    std::chrono::seconds(4);
    const auto stopRequested = [&]() {
        return g_stopRequested.load(std::memory_order_relaxed) ||
               globalSessionStop->stopRequested() ||
               (sessionControl && sessionControl->stopRequested());
    };
    do {
        error.clear();
        device = openSelectedIndex(options.deviceIndex, stopRequested, error);
        if (device || stopRequested()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } while (std::chrono::steady_clock::now() < deviceOpenDeadline);
    if (!device) {
        const std::string message = "APEX identity check failed: " + error;
        std::cerr << message << '\n';
        return failSession(3, message);
    }
    const bool apex6Pro = device->identity() && device->identity()->isApex6();
    if (options.apex6HapticGainExplicit && !apex6Pro) {
        constexpr std::string_view message =
            "--apex6-haptic-gain requires a verified Apex 6 Pro.";
        std::cerr << message << '\n';
        return failSession(2, message);
    }
    if (sessionControl) sessionControl->markProgress(asb::platform::SessionProgress::ControllerVerified);
    const bool apex5 = device->identity() && device->identity()->isApex5();
    const bool apex4 = device->identity() && device->identity()->isApex4();
    const auto verifiedCalibrationModel = apex4 ? "apex4" : apex5 ? "apex5" : apex6Pro ? "apex6" : "";
    if (!applyControllerCalibration(options, verifiedCalibrationModel, error)) {
        std::cerr << error << '\n';
        return failSession(2, error);
    }
    if (options.controllerCalibrations[0] || options.controllerCalibrations[1] || options.controllerCalibrations[2])
        std::cout << "Controller calibration selected after verification: " << verifiedCalibrationModel << '\n';
    const auto apex4TriggerCapability =
        asb::flydigi::classifyApex4TriggerInterface(device->info());
    if (apex4) {
        const auto capabilityName =
            apex4TriggerCapability ==
                    asb::flydigi::Apex4TriggerInterfaceCapability::Full64Byte
                ? "full"
                : apex4TriggerCapability ==
                          asb::flydigi::Apex4TriggerInterfaceCapability::Degraded32Byte
                      ? "degraded"
                      : "unknown";
        std::cout << "Apex 4 trigger interface: output_report_length="
                  << device->info().outputReportLength
                  << ", capability=" << capabilityName << '\n'
                  << "Apex 4 gyro tuning: overall="
                  << options.apex4GyroStrengthPercent << "%, yaw="
                  << options.apex4GyroYawStrengthPercent << "%\n";
    }
    if (apex4TriggerCapability ==
        asb::flydigi::Apex4TriggerInterfaceCapability::Degraded32Byte) {
        std::cerr
            << "Warning: this Apex 4 is exposing the degraded 32-byte vendor "
               "identity. LT may work while RT is unavailable even when HID "
               "writes succeed. Reconnect the controller/receiver until "
               "'identify' reports the full 64-byte trigger interface.\n";
    }
    const auto tunePhysicalInput = [&](asb::dualsense::DualSenseInputState& state) {
        if (apex4) {
            asb::flydigi::tuneApex4Gyroscope(
                state, options.apex4GyroStrengthPercent,
                options.apex4GyroYawStrengthPercent);
        }
    };

    std::optional<asb::flydigi::ProfileStatus> originalProfile;
    std::optional<asb::flydigi::InputTransportStatus> originalInputTransport;
    bool profileSwitchRequired = false;
    bool profileSwitchAcknowledged = true;
    if (options.apexProfileSlot || options.syncLightbar) {
        if (!device->identity() || !device->identity()->isApex5()) {
            constexpr std::string_view message =
                "APEX profile and RGB control require a verified Apex 5.";
            std::cerr << message << '\n';
            return failSession(15, message);
        }

        asb::flydigi::ProfileStatus first{};
        asb::flydigi::ProfileStatus second{};
        if (!device->readProfileStatus(first, error)) {
            const std::string message =
                "Could not read the original Apex 5 profile: " + error;
            std::cerr << message << '\n';
            return failSession(15, message);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        error.clear();
        if (!device->readProfileStatus(second, error) ||
            first.rawSlot != second.rawSlot) {
            const std::string message = error.empty()
                ? "The Apex 5 profile changed during the startup preflight."
                : "Could not confirm the original Apex 5 profile: " + error;
            std::cerr << message << '\n';
            return failSession(15, message);
        }
        if (first.switchBank) {
            constexpr std::string_view message =
                "The Apex 5 is using its Nintendo Switch profile bank; "
                "XInput profile and RGB control were refused.";
            std::cerr << message << '\n';
            return failSession(15, message);
        }
        originalProfile = first;
        profileSwitchRequired = options.apexProfileSlot &&
                                first.slot != *options.apexProfileSlot;
        if (profileSwitchRequired) {
            asb::flydigi::InputTransportStatus transport{};
            error.clear();
            if (!device->readInputTransportStatus(transport, error)) {
                const std::string message =
                    "Could not read the original Apex 5 input transport: " + error;
                std::cerr << message << '\n';
                return failSession(15, message);
            }
            originalInputTransport = transport;
        }
    }
    if (apex5 && !originalInputTransport) {
        asb::flydigi::InputTransportStatus transport{};
        error.clear();
        if (!device->readInputTransportStatus(transport, error)) {
            std::cerr << "Warning: could not read the original Apex 5 input "
                         "transport; raw motion routing will remain unchanged: "
                      << error << '\n';
            error.clear();
        } else {
            originalInputTransport = transport;
        }
    }
    if (originalInputTransport) {
        std::cout << "Apex 5 input transport snapshot: controller_data="
                  << originalInputTransport->controllerData
                  << ", raw_data=" << originalInputTransport->rawData
                  << ", keyboard_data=" << originalInputTransport->keyboardData
                  << ", mouse_data=" << originalInputTransport->mouseData
                  << ", third_party_control=" << originalInputTransport->thirdPartyControl
                  << std::endl;
    }
    std::unique_ptr<asb::TriggerResetGuard> resetOnExit;
    if (!apex6Pro) {
        if (!device->clearAll(error)) {
            std::cerr << "Could not establish a Normal trigger baseline: " << error << '\n';
            return failSession(4, "Could not establish a Normal trigger baseline: " + error);
        }
        resetOnExit = std::make_unique<asb::TriggerResetGuard>(*device);
    }

    std::unique_ptr<asb::RumbleResetGuard> rumbleResetOnExit;
    if (options.routeRumble && !apex6Pro) {
        if (!device->stopRumble(error)) {
            std::cerr << "Could not establish a stopped grip-rumble baseline: "
                      << error << '\n';
            return failSession(12, "Could not establish a stopped grip-rumble baseline: " + error);
        }
        rumbleResetOnExit = std::make_unique<asb::RumbleResetGuard>(*device);
    }

    struct AsyncWriteStop {
        asb::flydigi::Apex5Device* device;
        ~AsyncWriteStop() { if (device) device->stopAsyncWrites(); }
    } asyncWriteStop{&*device};

    auto inputSource = asb::platform::openPhysicalInputSource(
        device->info(), options.xinputIndex, error);
    if (!inputSource) {
        std::cerr << "Mandatory physical-input proxy creation failed: " << error << '\n';
        return failSession(8, "Mandatory physical-input proxy creation failed: " + error);
    }
    std::string inputBackend(inputSource->backendName());
    if (device->identity()) {
        inputSource->setBatteryState(
            device->identity()->batteryPercent(),
            device->identity()->chargeState());
    }
    asb::dualsense::DualSenseInputState initialInput{};
    if (device->identity()) {
        initialInput.batteryPercent = device->identity()->batteryPercent();
        initialInput.chargeState = device->identity()->chargeState();
    }
    const auto initialStatus = inputSource->waitForState(
        initialInput,
        inputSource->eventDriven() ? std::chrono::milliseconds(1000)
                                   : std::chrono::milliseconds(1),
        error);
    if (initialStatus != asb::platform::PhysicalInputStatus::State) {
        const std::string message = error.empty()
            ? "The selected APEX produced no complete input state during initialization."
            : error;
        std::cerr << "Mandatory physical-input proxy validation failed: " << message << '\n';
        return failSession(8, "Mandatory physical-input proxy validation failed: " + message);
    }
    tunePhysicalInput(initialInput);
    const auto physicalInputReadyAt = std::chrono::steady_clock::now();

    const auto preexistingDualSensePaths = snapshotDualSensePaths();
    asb::platform::VirtualDualSenseAudioEndpointProtection audioProtection;
    std::string audioProtectionError;
    if (!audioProtection.capture(audioProtectionError)) {
        std::cerr << "Warning: Windows default-audio protection is unavailable: "
                  << audioProtectionError << '\n';
    }

    asb::haptics::HapticConfig hapticConfig{};
    hapticConfig.activationThreshold =
        static_cast<double>(options.hapticThresholdPercent) / 100.0;
    auto adaptiveBridge = !apex6Pro
        ? std::make_unique<asb::dualsense::AdaptiveTriggerBridge>(*device, options.triggerStrengthPercent)
        : std::unique_ptr<asb::dualsense::AdaptiveTriggerBridge>{};
    auto apex6Bridge = apex6Pro
        ? std::make_unique<asb::dualsense::Apex6HapticBridge>(
              *device, hapticConfig, options.routeRumble, options.triggerStrengthPercent,
              options.vibrationStrengthPercent, options.apex6HapticGainPercent)
        : std::unique_ptr<asb::dualsense::Apex6HapticBridge>{};
    if (apex6Bridge) {
        apex6Bridge->updateTriggerPositions(initialInput.l2, initialInput.r2);
    }
    auto rumbleBridge = options.routeRumble && !apex6Pro
        ? std::make_unique<asb::dualsense::RumbleBridge>(*device, hapticConfig, options.vibrationStrengthPercent)
        : std::unique_ptr<asb::dualsense::RumbleBridge>{};
    const auto lightbarSlot = options.apexProfileSlot.value_or(
        originalProfile ? originalProfile->slot : 0);
    auto lightbarBridge = options.syncLightbar
        ? std::make_unique<asb::dualsense::LightbarBridge>(*device, lightbarSlot)
        : std::unique_ptr<asb::dualsense::LightbarBridge>{};
    if (lightbarBridge && lightbarBridge->failed()) {
        const std::string message =
            "Could not initialize temporary RGB routing: " + lightbarBridge->error();
        std::cerr << message << '\n';
        return failSession(16, message);
    }
    asb::dualsense::VirtualDualSenseOptions backendOptions{};
    backendOptions.viiperExecutable = std::move(options.viiperExecutable);
    backendOptions.backend = options.virtualBackend;
    backendOptions.captureAudioHapticsWaveform = apex6Pro;
    auto virtualDualSense = asb::dualsense::createVirtualDualSense(std::move(backendOptions));
    asb::dualsense::VirtualDualSense::FeedbackHandler feedbackHandler;
    if (apex6Bridge) {
        feedbackHandler = [apex6 = apex6Bridge.get(),
                           lightbar = lightbarBridge.get()](const auto& feedback) {
            apex6->handle(feedback);
            if (lightbar) lightbar->handle(feedback);
        };
    } else if (lightbarBridge) {
        feedbackHandler = [adaptive = adaptiveBridge.get(),
                           rumble = rumbleBridge.get(),
                           lightbar = lightbarBridge.get()](const auto& feedback) {
            adaptive->handle(feedback);
            if (rumble) rumble->handle(feedback);
            lightbar->handle(feedback);
        };
    } else if (rumbleBridge) {
        feedbackHandler = [adaptive = adaptiveBridge.get(),
                           rumble = rumbleBridge.get()](const auto& feedback) {
            adaptive->handle(feedback);
            rumble->handle(feedback);
        };
    } else {
        feedbackHandler = [adaptive = adaptiveBridge.get()](const auto& feedback) {
            adaptive->handle(feedback);
        };
    }
    std::future<bool> audioProtectionFuture;
    bool audioProtectionOk = true;
    const auto probeFirmware = [&preexistingDualSensePaths, &audioProtection,
                                &audioProtectionError, &audioProtectionFuture,
                                inspectHapticFormat = apex6Pro && options.routeRumble]
        (std::string& probeError) {
        if (audioProtectionFuture.valid()) {
            const bool previousProtectionOk = audioProtectionFuture.get();
            if (!previousProtectionOk) {
                std::cerr << "Warning: Windows default-audio protection failed: "
                          << audioProtectionError << '\n';
            }
        }
        audioProtectionError.clear();
        // Start watching as soon as each virtual controller has accepted its
        // initial state. This covers both the first attach and an automatic
        // recreation without delaying HID readiness verification.
        audioProtectionFuture = std::async(
            std::launch::async,
            [&audioProtection, &audioProtectionError, inspectHapticFormat]() {
                return !audioProtection.captured() ||
                       audioProtection.protectAfterVirtualDualSenseStart(
                           std::chrono::milliseconds(2000), audioProtectionError, inspectHapticFormat);
            });
        return readNewVirtualDualSenseFirmware(
            preexistingDualSensePaths, std::chrono::milliseconds(3000), probeError);
    };
    const auto waitForRemoval = [&preexistingDualSensePaths](std::string& removalError) {
        return waitForNewVirtualDualSenseRemoval(
            preexistingDualSensePaths, std::chrono::milliseconds(2000), removalError);
    };

    asb::dualsense::VirtualDualSenseStartupResult startupResult{};
    if (!asb::dualsense::startVerifiedVirtualDualSense(
            *virtualDualSense, initialInput, feedbackHandler, probeFirmware,
            waitForRemoval, 2, startupResult, error)) {
        if (audioProtectionFuture.valid()) {
            (void)audioProtectionFuture.get();
        }
        std::cerr << error << '\n';
        const int exitCode =
            startupResult.failure ==
                    asb::dualsense::VirtualDualSenseStartupFailure::BackendOpen
                ? 6
                : startupResult.failure ==
                          asb::dualsense::VirtualDualSenseStartupFailure::InitialInput
                      ? 8
                      : 9;
        return failSession(exitCode, error);
    }
    const auto virtualInputReadyAt = startupResult.inputReadyAt;
    const auto firmwareCheckedAt = startupResult.verifiedAt;
    const auto virtualFirmware = startupResult.firmware;
    if (startupResult.attempts > 1) {
        std::cout << "Virtual DualSense readiness recovered after one automatic recreation.\n";
    }

    using MonitorPtr = std::unique_ptr<asb::platform::HidTransport,
                                       void (*)(asb::platform::HidTransport*)>;
    MonitorPtr virtualInputMonitor(nullptr, asb::platform::destroyHidTransport);
    if (options.verifyVirtualInput) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline && !virtualInputMonitor) {
            std::string enumerateError;
            const auto devices = asb::platform::enumerateHidDevices(enumerateError);
            for (const auto& info : devices) {
                if (info.vendorId == 0x054C && info.productId == 0x0CE6 &&
                    info.usagePage == 0x0001 && info.usage == 0x0005 &&
                    info.inputReportLength >= 64) {
                    std::string openError;
                    virtualInputMonitor.reset(asb::platform::createHidTransport(info, openError));
                    if (!virtualInputMonitor) error = std::move(openError);
                    break;
                }
            }
            if (!virtualInputMonitor) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!virtualInputMonitor) {
            virtualDualSense->close();
            std::cerr << "Could not open the virtual DualSense HID input for verification: "
                      << (error.empty() ? "interface not found" : error) << '\n';
            return failSession(9, "Could not open the virtual DualSense HID input for verification: " +
                                      (error.empty() ? std::string("interface not found") : error));
        }
    }

    asb::platform::TemporaryPhysicalControllerIsolation physicalIsolation;
    if (sessionControl) sessionControl->markProgress(asb::platform::SessionProgress::VirtualReady);
    if (!physicalIsolation.activate(
            device->info(), options.sessionToken.value_or(""),
            profileSwitchRequired
                ? std::optional<std::uint8_t>(originalProfile->slot)
                : std::nullopt,
            error, stopRequested)) {
        virtualDualSense->close();
        std::cerr << "Temporary APEX isolation failed: " << error << '\n';
        return failSession(11, "Temporary APEX isolation failed: " + error);
    }
    const auto isolationReadyAt = std::chrono::steady_clock::now();
    if (sessionControl) sessionControl->markProgress(asb::platform::SessionProgress::IsolationVerified);

    std::unique_ptr<asb::ApexProfileRestoreGuard> profileRestoreOnExit;
    const auto restoreTemporaryApexProfile =
        [&profileRestoreOnExit, &physicalIsolation](std::string& restoreError) {
            restoreError.clear();
            if (!profileRestoreOnExit) return true;
            if (!profileRestoreOnExit->restore(restoreError)) return false;
            if (!physicalIsolation.confirmApexProfileRestored(restoreError)) {
                return false;
            }
            return true;
        };
    const auto rollbackFailedProfileStartup =
        [&virtualDualSense, &profileRestoreOnExit, &physicalIsolation,
         &restoreTemporaryApexProfile, &failSession](int exitCode,
                                                     std::string message) {
            virtualDualSense->close();
            std::string profileRollbackError;
            bool profileRolledBack =
                restoreTemporaryApexProfile(profileRollbackError);
            std::string isolationRollbackError;
            const bool isolationRolledBack =
                physicalIsolation.restore(isolationRollbackError);
            if (isolationRolledBack && profileRestoreOnExit) {
                profileRolledBack = true;
                profileRestoreOnExit->dismiss();
            }
            if (!profileRolledBack) {
                message += "; original profile rollback failed: " +
                           profileRollbackError;
            }
            if (!isolationRolledBack) {
                message += "; controller session rollback failed: " +
                           isolationRollbackError;
            }
            std::cerr << message << '\n';
            return failSession(exitCode, message);
        };

    asb::platform::PhysicalInputSourceStats retiredInputStats{};
    bool inputTransportRecoveryArmed = false;
    unsigned int independentTriggerStartupRecoveries = 0;
    unsigned int independentTriggerRuntimeRecoveries = 0;
    asb::platform::IndependentTriggerLossPolicy independentTriggerLoss;
    const auto armInputTransportRecovery = [&]() {
        if (inputTransportRecoveryArmed) return true;
        if (!originalInputTransport) {
            error = "The original Apex 5 input transport is unknown; LT/RT recovery "
                    "was refused to preserve controller settings.";
            return false;
        }
        if (!physicalIsolation.armApexInputTransportRestore(
                originalInputTransport->controllerData,
                originalInputTransport->rawData, error)) return false;
        inputTransportRecoveryArmed = true;
        return true;
    };
    const bool inputTransportRefreshRequired =
        apex5 && originalInputTransport &&
        (profileSwitchRequired || !originalInputTransport->controllerData ||
         !originalInputTransport->rawData);
    if (inputTransportRefreshRequired) {
        if (!armInputTransportRecovery()) {
            return rollbackFailedProfileStartup(
                11, "Could not arm Apex 5 input-transport recovery: " + error);
        }
        accumulatePhysicalInputStats(retiredInputStats, inputSource->stats());
        inputSource.reset();
    }

    if (profileSwitchRequired) {
        profileRestoreOnExit = std::make_unique<asb::ApexProfileRestoreGuard>(
            *device, originalProfile->slot);
        error.clear();
        profileSwitchAcknowledged =
            device->applyProfile(*options.apexProfileSlot, error);
        const std::string applyError = error;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        asb::flydigi::ProfileStatus applied{};
        std::string statusError;
        const bool statusRead = device->readProfileStatus(applied, statusError);
        if (!statusRead || applied.switchBank ||
            applied.slot != *options.apexProfileSlot) {
            std::string message = "Could not verify the requested Apex 5 profile";
            if (!profileSwitchAcknowledged && !applyError.empty()) {
                message += ": " + applyError;
            } else if (!statusRead && !statusError.empty()) {
                message += ": " + statusError;
            } else if (statusRead) {
                message += ": controller reported raw slot " +
                           std::to_string(applied.rawSlot);
            }
            return rollbackFailedProfileStartup(15, std::move(message));
        }

        // Selecting an onboard profile can reapply that slot's trigger mode.
        // Re-establish the bridge's neutral output baseline before declaring
        // the session ready, without modifying the saved onboard profile.
        std::string baselineError;
        if (!device->clearAll(baselineError)) {
            return rollbackFailedProfileStartup(
                4, "Could not establish a Normal trigger baseline after the "
                   "Apex 5 profile switch: " + baselineError);
        }
        if (options.routeRumble && !device->stopRumble(baselineError)) {
            return rollbackFailedProfileStartup(
                12, "Could not establish a stopped grip-rumble baseline after "
                    "the Apex 5 profile switch: " + baselineError);
        }

        // The firmware applies the new profile about one second after its ACK.
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    }

    if (inputTransportRefreshRequired) {
        std::string transportError;
        if (!device->setInputTransport(true, true, transportError)) {
            return rollbackFailedProfileStartup(
                8, "Could not restart the Apex 5 physical input stream after "
                   "transport refresh: " + transportError);
        }

        const auto reopenDeadline = std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(1500);
        std::string reopenError;
        do {
            reopenError.clear();
            inputSource = asb::platform::openPhysicalInputSource(
                device->info(), options.xinputIndex, reopenError);
            if (!inputSource) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        } while (!inputSource && std::chrono::steady_clock::now() < reopenDeadline);
        if (!inputSource) {
            return rollbackFailedProfileStartup(
                8, "Physical input source recreation failed after transport refresh: " +
                       reopenError);
        }
        inputBackend = std::string(inputSource->backendName());
        if (device->identity()) {
            inputSource->setBatteryState(
                device->identity()->batteryPercent(),
                device->identity()->chargeState());
        }

        constexpr unsigned int kRequiredFreshReports = 3;
        unsigned int freshReports = 0;
        const auto validationDeadline = std::chrono::steady_clock::now() +
                                        std::chrono::milliseconds(1000);
        while (freshReports < kRequiredFreshReports &&
               std::chrono::steady_clock::now() < validationDeadline) {
            asb::dualsense::DualSenseInputState refreshedInput{};
            std::string validationError;
            const auto status = inputSource->waitForState(
                refreshedInput, std::chrono::milliseconds(100), validationError);
            if (status == asb::platform::PhysicalInputStatus::State) {
                tunePhysicalInput(refreshedInput);
                initialInput = refreshedInput;
                ++freshReports;
            } else if (status == asb::platform::PhysicalInputStatus::Disconnected ||
                       status == asb::platform::PhysicalInputStatus::Error) {
                return rollbackFailedProfileStartup(
                    8, "Physical input validation failed after transport refresh: " +
                           (validationError.empty()
                                ? std::string("the refreshed stream disconnected")
                                : validationError));
            }
        }
        if (freshReports < kRequiredFreshReports) {
            return rollbackFailedProfileStartup(
                8, "Physical input validation timed out after transport refresh; "
                   "the refreshed stream produced fewer than three reports.");
        }
        if (!virtualDualSense->updateInput(initialInput, error)) {
            return rollbackFailedProfileStartup(
                8, "Virtual DualSense resynchronization failed after transport refresh: " +
                       error);
        }
    }
    // Reuse the existing temporary-transport recovery and restore marker. An
    // ACK/readback saying rawData=true does not prove operator reports are live.
    // Reopen before changing routing so the new reader observes the restart.
    const auto restartIndependentTriggerStream =
        [&](asb::dualsense::DualSenseInputState& refreshed, std::string& restartError) {
            if (!armInputTransportRecovery()) {
                restartError = error;
                return false;
            }
            accumulatePhysicalInputStats(retiredInputStats, inputSource->stats());
            inputSource.reset();
            inputSource = asb::platform::openPhysicalInputSource(
                device->info(), options.xinputIndex, restartError);
            if (!inputSource) return false;
            if (device->identity()) inputSource->setBatteryState(
                device->identity()->batteryPercent(), device->identity()->chargeState());
            // Never disable controllerData or switch onboard profiles here.
            if (!device->setInputTransport(true, false, restartError) ||
                !device->setInputTransport(true, true, restartError)) return false;
            const auto status = inputSource->waitForState(
                refreshed, std::chrono::milliseconds(1000), restartError);
            if (status != asb::platform::PhysicalInputStatus::State) {
                if (restartError.empty()) restartError = "No mapped input state arrived after LT/RT stream restart.";
                return false;
            }
            // A generic XInput fallback must not masquerade as recovery of the
            // selected mapped/vendor source, especially under Full Screen Experience.
            if (!inputSource->requiresIndependentTriggers()) {
                restartError = "LT/RT recovery did not reacquire the Apex 5 combined-axis mapped/vendor source.";
                return false;
            }
            inputBackend = std::string(inputSource->backendName());
            return validateIndependentTriggerStream(
                *inputSource, refreshed, std::chrono::milliseconds(1000), restartError);
        };
    if (apex5 && inputSource->requiresIndependentTriggers()) {
        std::string triggerValidationError;
        if (!validateIndependentTriggerStream(
                *inputSource, initialInput, std::chrono::milliseconds(1000),
                triggerValidationError)) {
            std::cerr << "Apex 5 independent LT/RT stream unavailable; attempting one "
                         "temporary routing restart: " << triggerValidationError << std::endl;
            ++independentTriggerStartupRecoveries;
            if (!restartIndependentTriggerStream(initialInput, triggerValidationError)) {
                return rollbackFailedProfileStartup(
                    8, "Apex 5 independent LT/RT validation failed after one restart: " +
                           triggerValidationError);
            }
        }
        if (!virtualDualSense->updateInput(initialInput, error)) {
            return rollbackFailedProfileStartup(
                8, "Virtual DualSense LT/RT resynchronization failed: " + error);
        }
        std::cout << "Apex 5 independent LT/RT stream verified." << std::endl;
    } else if (apex5 && inputSource->stats().vendorStates == 0) {
        std::cerr << "Warning: the Apex 5 vendor motion stream produced no state "
                     "during initialization. Standard controls remain active; "
                     "motion data will be merged if the stream resumes.\n";
    }

    // Start only after every ordered startup/profile write. On Apex 4 this
    // moves paced feedback off the virtual-controller callback; Apex 5 keeps
    // its existing synchronous path.
    if (!device->startAsyncWrites(error)) {
        virtualDualSense->close();
        return failSession(11, "Could not start the APEX feedback writer: " + error);
    }
    if (apex6Bridge && !apex6Bridge->start(error)) {
        virtualDualSense->close();
        return failSession(11, "Could not start the Apex 6 haptic stream: " + error);
    }

    if (apex6Pro && options.routeRumble && audioProtectionFuture.valid()) {
        // Prepared launch must see the audio preflight result before the game
        // opens this endpoint. APEX 4/5 retain their asynchronous startup path.
        audioProtectionOk = audioProtectionFuture.get();
        const auto format = audioProtection.hapticFormat();
        std::cout << "apex6_audio_format="
                  << asb::platform::hapticAudioFormatStatusName(format.status) << '\n'
                  << "apex6_audio_mix_channels=" << format.channels << '\n'
                  << "apex6_audio_channel_mask=" << format.channelMask << '\n'
                  << "apex6_audio_physical_speaker_mask=" << format.physicalSpeakerMask << std::endl;
        if (format.status != asb::platform::HapticAudioFormatStatus::Quadraphonic) {
            std::cerr << "Warning: virtual DualSense quadraphonic audio is not verified. "
                         "Native grip haptics may be silent. While the bridge is active, open "
                         "mmsys.cpl > Playback > Wireless Controller > Configure > Quadraphonic. "
                         "Keep your normal speakers as the default output, then restart the game "
                         "after configuration (or after recreating the bridge).\n";
        }
    }
    if (apex6Pro && options.routeRumble) {
        std::cout << "apex6_pcm_gain_percent=" << options.apex6HapticGainPercent << std::endl;
        std::cout << "apex6_haptic_threshold_percent=0" << std::endl;
        if (options.apex6HapticGainPercent > 100) {
            std::cerr << "Experimental Apex 6 native PCM gain enabled: quiet amplitudes are "
                         "increased with bounded compression, not a linear multiplier. "
                         "Trigger effects and native frequencies are not retuned.\n";
        }
    }
    if (sessionControl) {
        const bool readyPublished = sessionControl->publish(asb::platform::SessionPhase::Ready, 0,
                                     "ASB_READY|" + device->identity()->describe(), error);
        if (readyPublished) sessionControl->markProgress(asb::platform::SessionProgress::RuntimeReady);
        if (!readyPublished ||
            !sessionControl->signalReady(error)) {
            virtualDualSense->close();
            const std::string signalError = error;
            std::string profileRollbackError;
            bool profileRolledBack =
                restoreTemporaryApexProfile(profileRollbackError);
            std::string isolationRollbackError;
            const bool isolationRolledBack =
                physicalIsolation.restore(isolationRollbackError);
            if (isolationRolledBack && profileRestoreOnExit) {
                profileRolledBack = true;
                profileRestoreOnExit->dismiss();
            }
            std::string message =
                "Bridge session ready signal failed: " + signalError;
            if (!profileRolledBack) {
                message += "; original profile rollback failed: " +
                           profileRollbackError;
            }
            if (!isolationRolledBack) {
                message += "; controller session rollback failed: " +
                           isolationRollbackError;
            }
            std::cerr << message << '\n';
            return failSession(13, message);
        }
    }

    const auto initializedAt = std::chrono::steady_clock::now();
    const auto initializationMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            initializedAt - initializationStartedAt).count();
    const auto physicalInputInitializationMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            physicalInputReadyAt - initializationStartedAt).count();
    const auto virtualInputInitializationMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            virtualInputReadyAt - physicalInputReadyAt).count();
    const auto firmwareInitializationMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            firmwareCheckedAt - virtualInputReadyAt).count();
    const auto isolationInitializationMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            isolationReadyAt - firmwareCheckedAt).count();
    const auto processUsageStarted = processUsageSnapshot();

    std::cout << "APEX verified: " << device->identity()->describe() << '\n'
              << (apex6Pro
                      ? "Apex 6 trigger-vibration routing enabled.\n"
                      : "Adaptive-trigger routing enabled.\n")
              << (apex6Pro && options.routeRumble
                      ? "Apex 6 grip voice-coil and DualSense audio-haptics routing enabled.\n"
                      : rumbleBridge
                          ? "Grip-rumble and DualSense audio-haptics routing enabled.\n"
                          : "Grip-rumble and audio haptics routing remain disabled.\n")
              << (lightbarBridge
                      ? "DualSense lightbar RGB synchronization enabled.\n"
                      : "")
              << "All APEX controls are proxied through " << inputBackend
              << " into the virtual DualSense.\n"
              << "Virtual DualSense backend: "
              << virtualDualSense->stats().backendVersion << ".\n"
              << (virtualFirmware
                      ? "Virtual DualSense firmware " +
                            hex16(virtualFirmware->updateVersion) +
                            (virtualFirmware->updateVersion >= 0x0630
                                 ? " verified.\n"
                                 : " is obsolete; newer games may disable native feedback.\n")
                      : "")
              << (virtualInputMonitor ? "Virtual DualSense HID input verification is enabled.\n" : "")
              << (options.touchpadProfile != asb::dualsense::TouchpadGestureProfile::None
                      ? "Touchpad gesture profile: " +
                            std::string(asb::dualsense::touchpadGestureProfileName(
                                options.touchpadProfile)) + ".\n"
                      : "")
              << (options.apexProfileSlot
                      ? "Apex onboard profile: " +
                            std::to_string(*options.apexProfileSlot + 1) +
                            (originalProfile && originalProfile->slot !=
                                                    *options.apexProfileSlot
                                 ? " (temporary; original profile " +
                                       std::to_string(originalProfile->slot + 1) +
                                       " will be restored).\n"
                                 : " (already active).\n")
                      : "")
              << (physicalIsolation.active()
                      ? "The original APEX game interface is hidden for this bridge session only.\n"
                      : "")
              << (sessionControl ? "Bridge session IPC is ready.\n" : "")
              << (rumbleBridge
                      ? "Audio-haptics activation threshold: " +
                            std::to_string(options.hapticThresholdPercent) + "%\n"
                      : "")
              << (options.duration ? "Bridge running...\n" : "Bridge running; press Ctrl+C to stop cleanly.\n");
    const auto started = initializedAt;
    bool disconnected = false;
    bool inputProxyFailed = false;
    std::string inputProxyError;
    std::optional<asb::dualsense::DualSenseInputState> lastPhysicalInput = initialInput;
    std::optional<asb::dualsense::DualSenseInputState> lastForwardedInput = initialInput;
    std::uint64_t inputSamples = 1;
    std::uint64_t buttonTransitions = 0;
    std::uint16_t seenButtons = 0;
    std::uint8_t seenDpad = 0;
    std::uint8_t maximumL2 = 0;
    std::uint8_t maximumR2 = 0;
    std::uint8_t maximumSimultaneousTriggers = 0;
    std::uint64_t simultaneousTriggerReports = 0;
    std::uint8_t minimumRightStickX = initialInput.rx;
    std::uint8_t maximumRightStickX = initialInput.rx;
    std::uint8_t minimumRightStickY = initialInput.ry;
    std::uint8_t maximumRightStickY = initialInput.ry;
    std::uint64_t virtualInputReports = 0;
    std::uint8_t virtualSeenFace = 0;
    std::uint8_t virtualSeenShoulders = 0;
    std::uint8_t virtualSeenSystem = 0;
    std::uint16_t virtualSeenDpadHats = 0;
    std::uint8_t virtualMaximumL2 = 0;
    std::uint8_t virtualMaximumR2 = 0;
    std::uint8_t virtualMaximumSimultaneousTriggers = 0;
    std::uint64_t virtualSimultaneousTriggerReports = 0;
    std::uint8_t virtualMinimumRightStickX = 0xFF;
    std::uint8_t virtualMaximumRightStickX = 0;
    std::uint8_t virtualMinimumRightStickY = 0xFF;
    std::uint8_t virtualMaximumRightStickY = 0;
    std::uint64_t virtualTouchStarts = 0;
    std::uint64_t virtualTouchActiveReports = 0;
    std::uint64_t virtualTouchMovementReports = 0;
    std::uint64_t coalescedInputReports = 0;
    std::uint64_t keepaliveInputReports = 0;
    std::uint64_t forwardedPhysicalReports = 1;
    MicrosecondLatencyHistogram forwardingLatency;
    std::uint16_t virtualTouchMinimumX = 0xFFFF;
    std::uint16_t virtualTouchMaximumX = 0;
    std::uint16_t virtualTouchMinimumY = 0xFFFF;
    std::uint16_t virtualTouchMaximumY = 0;
    bool virtualTouchWasActive = false;
    std::uint16_t previousVirtualTouchX = 0;
    std::uint16_t previousVirtualTouchY = 0;
    std::vector<std::uint8_t> virtualInputBuffer(64, 0);
    ButtonHoldTracker mappedTouchpadHold;
    ButtonHoldTracker virtualTouchpadHold;
    asb::dualsense::TouchpadGestureMapper touchpadGestureMapper(
        options.touchpadProfile);
    auto lastInputForwardedAt = std::chrono::steady_clock::now();
    constexpr auto kInputKeepalive = std::chrono::milliseconds(100);
    auto lastBatteryRefreshAt = std::chrono::steady_clock::now();
    constexpr auto kBatteryRefreshInterval = std::chrono::seconds(15);
    asb::platform::PhysicalInputFreshnessWatchdog inputFreshness(
        std::chrono::seconds(1), started);
    asb::Apex4GyroValidation apex4GyroValidation;
    std::string asyncWriteError;
    while (!g_stopRequested.load(std::memory_order_relaxed) &&
           !globalSessionStop->stopRequested() &&
           (!sessionControl || !sessionControl->stopRequested()) &&
           (!adaptiveBridge || !adaptiveBridge->failed()) &&
           (!apex6Bridge || !apex6Bridge->failed()) &&
           (!rumbleBridge || !rumbleBridge->failed()) &&
           !device->takeAsyncWriteError(asyncWriteError)) {
        const auto loopNow = std::chrono::steady_clock::now();
        if (device->identity() && device->identity()->isApex5() &&
            loopNow - lastBatteryRefreshAt >= kBatteryRefreshInterval) {
            lastBatteryRefreshAt = loopNow;
            std::string refreshError;
            (void)device->requestBatteryRefresh(refreshError);
        }
        asb::dualsense::DualSenseInputState input{};
        const auto inputWait = inputSource->eventDriven()
            ? std::chrono::milliseconds(8)
            : std::chrono::milliseconds(1);
        auto inputStatus = inputSource->waitForState(
            input, inputWait, inputProxyError);
        const bool independentTriggersStale =
            (inputStatus == asb::platform::PhysicalInputStatus::State ||
             inputStatus == asb::platform::PhysicalInputStatus::Timeout) &&
            inputSource->requiresIndependentTriggers() &&
            !inputSource->independentTriggersReady();
        if (!independentTriggersStale) {
            independentTriggerLoss.streamReady();
        }
        // Stale states still forward mapped input; independent bytes are withheld.
        const auto independentTriggerAction = independentTriggersStale
            ? independentTriggerLoss.streamStale()
            : asb::platform::IndependentTriggerLossPolicy::Action::Continue;
        if (independentTriggerAction != asb::platform::IndependentTriggerLossPolicy::Action::Continue) {
            // Mapped traffic cannot keep a missing independent stream "healthy".
            // Stop forwarding before any canceled or stale trigger state leaks.
            bool recovered = false;
            if (independentTriggerAction == asb::platform::IndependentTriggerLossPolicy::Action::Recover) {
                ++independentTriggerRuntimeRecoveries;
                independentTriggerLoss.recoveryAttempted();
                std::cerr << "Apex 5 independent LT/RT stream lost; attempting a "
                             "temporary routing restart (" << independentTriggerLoss.recentRecoveries()
                          << "/" << asb::platform::IndependentTriggerLossPolicy::kMaximumRecoveriesPerWindow
                          << " in 10 min)." << std::endl;
                asb::dualsense::DualSenseInputState neutral{};
                neutral.batteryPercent = input.batteryPercent;
                neutral.chargeState = input.chargeState;
                if (virtualDualSense->updateInput(neutral, inputProxyError)) {
                    lastForwardedInput = neutral;
                    recovered = restartIndependentTriggerStream(input, inputProxyError);
                }
            } else {
                inputProxyError = "The independent Apex 5 LT/RT stream stayed unavailable after " +
                    std::to_string(asb::platform::IndependentTriggerLossPolicy::kMaximumRecoveriesPerWindow) +
                    " recoveries within 10 minutes.";
            }
            if (!recovered) {
                inputProxyFailed = true;
                if (sessionControl) sessionControl->markInterrupted(
                    asb::platform::SessionInterruption::InputStreamLost);
                if (inputProxyError.empty()) inputProxyError = "Independent Apex 5 LT/RT recovery failed.";
                break;
            }
            inputStatus = asb::platform::PhysicalInputStatus::State;
            std::cout << "Apex 5 independent LT/RT stream recovered." << std::endl;
        }
        bool forwardInput = false;
        const auto inputObservedAt = std::chrono::steady_clock::now();
        if (inputStatus == asb::platform::PhysicalInputStatus::State) {
            tunePhysicalInput(input);
            if (apex4 && apex4GyroValidation.observe(input, inputObservedAt)) {
                std::cerr << "WARNING: [bridge] APEX 4 motion sensors report zero. "
                          << "In Flydigi Space Station, set the active profile's gyro to "
                          << "'Mouse, always on' to enable DualSense gyro aiming.\n";
            }
            inputFreshness.observeFreshState(inputObservedAt);
            if (apex6Bridge) {
                apex6Bridge->updateTriggerPositions(input.l2, input.r2);
            }
            ++inputSamples;
            seenButtons = static_cast<std::uint16_t>(seenButtons | input.buttons);
            seenDpad = static_cast<std::uint8_t>(seenDpad | input.dpad);
            if (input.l2 > maximumL2) maximumL2 = input.l2;
            if (input.r2 > maximumR2) maximumR2 = input.r2;
            maximumSimultaneousTriggers = (std::max)(
                maximumSimultaneousTriggers, (std::min)(input.l2, input.r2));
            if (input.l2 > 30 && input.r2 > 30) {
                ++simultaneousTriggerReports;
            }
            minimumRightStickX = (std::min)(minimumRightStickX, input.rx);
            maximumRightStickX = (std::max)(maximumRightStickX, input.rx);
            minimumRightStickY = (std::min)(minimumRightStickY, input.ry);
            maximumRightStickY = (std::max)(maximumRightStickY, input.ry);
            if (lastPhysicalInput && lastPhysicalInput->buttons != input.buttons) {
                ++buttonTransitions;
            }
            lastPhysicalInput = input;
            mappedTouchpadHold.observe(
                (input.buttons & asb::dualsense::button::kTouchpadClick) != 0,
                inputObservedAt);
            if (options.touchpadProfile != asb::dualsense::TouchpadGestureProfile::None) {
                touchpadGestureMapper.transform(input, inputObservedAt);
            }
            // Changed states go out immediately; unchanged ones only as a keepalive.
            forwardInput = !lastForwardedInput || *lastForwardedInput != input ||
                           (inputSource->eventDriven() &&
                            inputObservedAt - lastInputForwardedAt >= kInputKeepalive);
            if (!forwardInput) ++coalescedInputReports;
        } else if (inputStatus == asb::platform::PhysicalInputStatus::Timeout) {
            if (inputSource->eventDriven() && inputFreshness.expired(inputObservedAt)) {
                inputProxyFailed = true;
                if (sessionControl) sessionControl->markInterrupted(asb::platform::SessionInterruption::InputStreamLost);
                inputProxyError =
                    "The mandatory physical APEX input stream produced no fresh "
                    "report for one second.";
                break;
            }
            if (lastPhysicalInput) {
                input = *lastPhysicalInput;
                if (options.touchpadProfile != asb::dualsense::TouchpadGestureProfile::None) {
                    touchpadGestureMapper.transform(input, inputObservedAt);
                }
                forwardInput = !lastForwardedInput ||
                               *lastForwardedInput != input ||
                               inputObservedAt - lastInputForwardedAt >= kInputKeepalive;
            }
        } else {
            inputProxyFailed = true;
            if (sessionControl) sessionControl->markInterrupted(inputStatus == asb::platform::PhysicalInputStatus::Disconnected
                ? asb::platform::SessionInterruption::PhysicalDisconnected
                : asb::platform::SessionInterruption::InputStreamLost);
            if (inputProxyError.empty()) {
                inputProxyError = inputStatus == asb::platform::PhysicalInputStatus::Disconnected
                    ? "The mandatory physical APEX input source disconnected."
                    : "The mandatory physical APEX input source failed.";
            }
            break;
        }
        if (forwardInput) {
            if (!virtualDualSense->updateInput(input, inputProxyError)) {
                inputProxyFailed = true;
                break;
            }
            forwardingLatency.observe(std::chrono::steady_clock::now() - inputObservedAt);
            if (inputStatus == asb::platform::PhysicalInputStatus::State &&
                (!lastForwardedInput || *lastForwardedInput != input)) {
                ++forwardedPhysicalReports;
            } else {
                ++keepaliveInputReports;
            }
            lastForwardedInput = input;
            lastInputForwardedAt = inputObservedAt;
        }
        if (virtualInputMonitor) {
            std::size_t bytesRead = 0;
            std::string readError;
            const auto readStatus = virtualInputMonitor->readInputReport(
                virtualInputBuffer, std::chrono::milliseconds(1), bytesRead, readError);
            if (readStatus == asb::platform::HidReadStatus::Error) {
                inputProxyFailed = true;
                inputProxyError = "Virtual DualSense HID verification failed: " + readError;
                break;
            }
            if (readStatus == asb::platform::HidReadStatus::Data &&
                bytesRead >= 11 && virtualInputBuffer[0] == 0x01) {
                ++virtualInputReports;
                const auto hat = static_cast<std::uint8_t>(virtualInputBuffer[8] & 0x0F);
                if (hat < 16) {
                    virtualSeenDpadHats = static_cast<std::uint16_t>(
                        virtualSeenDpadHats | (std::uint16_t{1} << hat));
                }
                virtualSeenFace = static_cast<std::uint8_t>(
                    virtualSeenFace | (virtualInputBuffer[8] & 0xF0));
                virtualSeenShoulders = static_cast<std::uint8_t>(
                    virtualSeenShoulders | virtualInputBuffer[9]);
                virtualSeenSystem = static_cast<std::uint8_t>(
                    virtualSeenSystem | virtualInputBuffer[10]);
                virtualTouchpadHold.observe(
                    (virtualInputBuffer[10] & 0x02) != 0,
                    std::chrono::steady_clock::now());
                if (bytesRead >= 37) {
                    const bool touchActive = (virtualInputBuffer[33] & 0x80) == 0;
                    if (touchActive) {
                        const auto touchX = static_cast<std::uint16_t>(
                            virtualInputBuffer[34] |
                            ((virtualInputBuffer[35] & 0x0F) << 8));
                        const auto touchY = static_cast<std::uint16_t>(
                            (virtualInputBuffer[35] >> 4) |
                            (virtualInputBuffer[36] << 4));
                        ++virtualTouchActiveReports;
                        if (!virtualTouchWasActive) ++virtualTouchStarts;
                        if (virtualTouchWasActive &&
                            (touchX != previousVirtualTouchX ||
                             touchY != previousVirtualTouchY)) {
                            ++virtualTouchMovementReports;
                        }
                        virtualTouchMinimumX = (std::min)(virtualTouchMinimumX, touchX);
                        virtualTouchMaximumX = (std::max)(virtualTouchMaximumX, touchX);
                        virtualTouchMinimumY = (std::min)(virtualTouchMinimumY, touchY);
                        virtualTouchMaximumY = (std::max)(virtualTouchMaximumY, touchY);
                        previousVirtualTouchX = touchX;
                        previousVirtualTouchY = touchY;
                    }
                    virtualTouchWasActive = touchActive;
                }
                if (virtualInputBuffer[5] > virtualMaximumL2) virtualMaximumL2 = virtualInputBuffer[5];
                if (virtualInputBuffer[6] > virtualMaximumR2) virtualMaximumR2 = virtualInputBuffer[6];
                virtualMaximumSimultaneousTriggers = (std::max)(
                    virtualMaximumSimultaneousTriggers,
                    (std::min)(virtualInputBuffer[5], virtualInputBuffer[6]));
                if (virtualInputBuffer[5] > 30 && virtualInputBuffer[6] > 30) {
                    ++virtualSimultaneousTriggerReports;
                }
                virtualMinimumRightStickX =
                    (std::min)(virtualMinimumRightStickX, virtualInputBuffer[3]);
                virtualMaximumRightStickX =
                    (std::max)(virtualMaximumRightStickX, virtualInputBuffer[3]);
                virtualMinimumRightStickY =
                    (std::min)(virtualMinimumRightStickY, virtualInputBuffer[4]);
                virtualMaximumRightStickY =
                    (std::max)(virtualMaximumRightStickY, virtualInputBuffer[4]);
            }
        }
        if (!virtualDualSense->connected()) {
            disconnected = true;
            if (sessionControl) sessionControl->markInterrupted(asb::platform::SessionInterruption::VirtualDisconnected);
            break;
        }
        if (options.duration && std::chrono::steady_clock::now() - started >= *options.duration) break;
    }
    const auto runtimeMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    const auto trackingFinishedAt = std::chrono::steady_clock::now();
    // A feedback writer can notice unplugging before the input reader does.
    // Only classify it as physical loss after a successful enumeration proves
    // that this exact selected interface disappeared. Never infer it from an
    // arbitrary write error, nor from an explicit owner/user stop.
    if (sessionControl && !g_stopRequested.load(std::memory_order_relaxed) &&
        !globalSessionStop->stopRequested() && !sessionControl->stopRequested() &&
        (inputProxyFailed || (adaptiveBridge && adaptiveBridge->failed()) ||
         (apex6Bridge && apex6Bridge->failed()) || (rumbleBridge && rumbleBridge->failed()) || !asyncWriteError.empty())) {
        std::string enumerationError;
        const auto devices = asb::platform::enumerateHidDevices(enumerationError);
        if (enumerationError.empty() && std::none_of(devices.begin(), devices.end(),
            [&device](const auto& candidate) { return candidate.path == device->info().path; }))
            sessionControl->markInterrupted(asb::platform::SessionInterruption::PhysicalDisconnected);
    }
    mappedTouchpadHold.finish(trackingFinishedAt);
    virtualTouchpadHold.finish(trackingFinishedAt);
    if (audioProtectionFuture.valid()) audioProtectionOk = audioProtectionFuture.get();
    if (!audioProtectionOk) {
        std::cerr << "Warning: Windows default-audio protection failed: "
                  << audioProtectionError << '\n';
    }

    if (sessionControl) {
        std::string ignored;
        (void)sessionControl->publish(asb::platform::SessionPhase::Stopping, 0,
                                      "Bridge cleanup in progress.", ignored);
    }
    // Playnite Fullscreen regains focus as soon as the game stops. Clear the
    // last forwarded button state before detaching the virtual controller so a
    // held Cross/A press cannot become a new launch command in Playnite.
    std::string neutralizationError;
    const bool virtualInputNeutralized = virtualDualSense->updateInput(
        asb::dualsense::DualSenseInputState{}, neutralizationError);
    if (virtualInputNeutralized) {
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
    } else {
        std::cerr << "Warning: virtual input neutralization during cleanup failed: "
                  << neutralizationError << '\n';
    }
    virtualDualSense->close(); // joins the feedback callback before touching the HID device
    device->stopAsyncWrites(); // no queued effect may overtake the reset below
    std::string apex6ResetError;
    const bool apex6ResetOk = !apex6Bridge || apex6Bridge->stop(apex6ResetError);
    const auto virtualStats = virtualDualSense->stats();
    const auto touchpadGestureStats = touchpadGestureMapper.stats();
    const auto bridgeStats = adaptiveBridge
        ? adaptiveBridge->stats()
        : asb::dualsense::AdaptiveTriggerBridgeStats{};
    const auto apex6Stats = apex6Bridge
        ? apex6Bridge->stats()
        : asb::dualsense::Apex6HapticBridgeStats{};
    const auto rumbleStats = rumbleBridge
        ? rumbleBridge->stats()
        : asb::dualsense::RumbleBridgeStats{};
    if (lightbarBridge) lightbarBridge->restore();
    const auto lightbarStats = lightbarBridge
        ? lightbarBridge->stats()
        : asb::dualsense::LightbarBridgeStats{};
    std::string rumbleResetError;
    const bool rumbleResetOk = !rumbleBridge || device->stopRumble(rumbleResetError);
    if (rumbleResetOk && rumbleResetOnExit) rumbleResetOnExit->dismiss();
    // Observe a stable release while the raw stream is still owned and live.
    const bool physicalControlsReleased = inputSource && waitForPhysicalControlsReleased(
        *inputSource, std::chrono::milliseconds(1500));
    std::string resetError;
    const bool resetOk = apex6Pro ? apex6ResetOk : device->clearAll(resetError);
    if (apex6Pro && !apex6ResetOk) resetError = apex6ResetError;
    if (resetOk && resetOnExit) resetOnExit->dismiss();
    // Keep HidHide active until launch-capable controls have been released for
    // a short stable interval. The wait is bounded so disconnects and damaged
    // devices can never prevent restoration/uninstall.
    std::string profileRestoreError;
    bool profileRestored =
        restoreTemporaryApexProfile(profileRestoreError);
    std::string isolationRestoreError;
    const bool isolationRestored = physicalIsolation.restore(isolationRestoreError);
    // A successful watchdog-backed session restore also proves the saved
    // onboard profile was restored, even if the first in-process attempt failed.
    if (isolationRestored && profileRestoreOnExit) {
        profileRestored = true;
        profileRestoreOnExit->dismiss();
    }
    auto inputSourceStats = retiredInputStats;
    if (inputSource) accumulatePhysicalInputStats(inputSourceStats, inputSource->stats());
    const auto processUsageFinished = processUsageSnapshot();
    const double runtimeSeconds = runtimeMilliseconds > 0
        ? static_cast<double>(runtimeMilliseconds) / 1000.0
        : 0.0;
    const double physicalReportRateHz = runtimeSeconds > 0.0
        ? static_cast<double>(inputSourceStats.reports) / runtimeSeconds
        : 0.0;
    // The backend setter only runs when the physical state changes, while the
    // virtual USB controller keeps emitting complete HID reports. When the
    // verification monitor is enabled, report the observed HID cadence rather
    // than the (usually much lower) state-update cadence.
    const std::uint64_t measuredVirtualReports = virtualInputMonitor
        ? virtualInputReports
        : virtualStats.inputUpdates;
    const double virtualReportRateHz = runtimeSeconds > 0.0
        ? static_cast<double>(measuredVirtualReports) / runtimeSeconds
        : 0.0;
    const auto cpuDelta100ns = processUsageFinished.cpu100ns >= processUsageStarted.cpu100ns
        ? processUsageFinished.cpu100ns - processUsageStarted.cpu100ns
        : 0;
    const double cpuPercent = runtimeSeconds > 0.0
        ? (static_cast<double>(cpuDelta100ns) / 10000000.0) /
              runtimeSeconds / static_cast<double>(logicalProcessorCount()) * 100.0
        : 0.0;
    const auto lostInputReports = inputSourceStats.parseFailures +
        static_cast<std::uint64_t>(inputProxyFailed ? 1 : 0);

    if (!options.telemetryJson.empty()) {
        BridgeTelemetry telemetry{};
        telemetry.virtualStats = virtualStats;
        if (apex6Pro) {
            telemetry.apex6Stats = apex6Stats;
            telemetry.apex6AudioFormat = audioProtection.hapticFormat();
        }
        telemetry.physicalStats = inputSourceStats;
        telemetry.processUsage = processUsageFinished;
        telemetry.inputBackend = inputBackend;
        telemetry.independentTriggerStartupRecoveries = independentTriggerStartupRecoveries;
        telemetry.independentTriggerRuntimeRecoveries = independentTriggerRuntimeRecoveries;
        telemetry.virtualInputMonitorEnabled = virtualInputMonitor != nullptr;
        telemetry.startupAttempts = startupResult.attempts;
        telemetry.initializationMilliseconds = initializationMilliseconds;
        telemetry.physicalInputInitializationMilliseconds =
            physicalInputInitializationMilliseconds;
        telemetry.virtualInputInitializationMilliseconds =
            virtualInputInitializationMilliseconds;
        telemetry.firmwareInitializationMilliseconds =
            firmwareInitializationMilliseconds;
        telemetry.isolationInitializationMilliseconds =
            isolationInitializationMilliseconds;
        telemetry.runtimeMilliseconds = runtimeMilliseconds;
        telemetry.latencyP50Us = forwardingLatency.percentile(50);
        telemetry.latencyP95Us = forwardingLatency.percentile(95);
        telemetry.latencyP99Us = forwardingLatency.percentile(99);
        telemetry.latencySamples = forwardingLatency.samples();
        telemetry.physicalReportRateHz = physicalReportRateHz;
        telemetry.virtualReportRateHz = virtualReportRateHz;
        telemetry.forwardedPhysicalReports = forwardedPhysicalReports;
        telemetry.keepaliveReports = keepaliveInputReports;
        telemetry.lostReports = lostInputReports;
        telemetry.coalescedReports = coalescedInputReports;
        telemetry.maximumSimultaneousTriggers = maximumSimultaneousTriggers;
        telemetry.simultaneousTriggerReports = simultaneousTriggerReports;
        telemetry.virtualMaximumSimultaneousTriggers =
            virtualMaximumSimultaneousTriggers;
        telemetry.virtualSimultaneousTriggerReports =
            virtualSimultaneousTriggerReports;
        telemetry.batteryPercent = lastPhysicalInput
            ? lastPhysicalInput->batteryPercent : initialInput.batteryPercent;
        telemetry.chargeState = lastPhysicalInput
            ? lastPhysicalInput->chargeState : initialInput.chargeState;
        telemetry.cpuPercent = cpuPercent;

        std::string telemetryError;
        if (!writeBridgeTelemetryFile(
                options.telemetryJson, telemetry, telemetryError)) {
            std::cerr << "Warning: " << telemetryError << '\n';
        }
    }

    std::cout << "apex_routing="
              << (apex6Pro ? "realtime-voice-coils" : "adaptive-triggers") << '\n'
              << "virtual_backend=" << virtualStats.backendVersion << '\n'
              << "input_mode=mandatory-full-proxy\n"
              << "input_backend=" << inputBackend << '\n'
              << "input_event_driven=" << (inputSource && inputSource->eventDriven() ? "yes" : "no") << '\n'
              << "independent_trigger_startup_recoveries=" << independentTriggerStartupRecoveries << '\n'
              << "independent_trigger_runtime_recoveries=" << independentTriggerRuntimeRecoveries << '\n'
              << "input_vendor_reports=" << inputSourceStats.vendorReports << '\n'
              << "input_vendor_states=" << inputSourceStats.vendorStates << '\n'
              << "input_vendor_parse_failures="
              << inputSourceStats.vendorParseFailures << '\n'
              << "input_vendor_read_failures="
              << inputSourceStats.vendorReadFailures << '\n'
              << "virtual_startup_attempts=" << startupResult.attempts << '\n'
              << "runtime_ms=" << runtimeMilliseconds << '\n'
              << "initialization_ms=" << initializationMilliseconds << '\n'
              << "initialization_physical_input_ms="
              << physicalInputInitializationMilliseconds << '\n'
              << "initialization_virtual_input_ms="
              << virtualInputInitializationMilliseconds << '\n'
              << "initialization_firmware_ms="
              << firmwareInitializationMilliseconds << '\n'
              << "initialization_isolation_ms="
              << isolationInitializationMilliseconds << '\n'
              << "backend_initialization_bootstrap_us="
              << virtualStats.initializationBootstrapUs << '\n'
              << "backend_initialization_server_us="
              << virtualStats.initializationServerUs << '\n'
              << "backend_initialization_bus_us="
              << virtualStats.initializationBusUs << '\n'
              << "backend_initialization_device_us="
              << virtualStats.initializationDeviceUs << '\n'
              << "backend_initialization_feedback_us="
              << virtualStats.initializationFeedbackUs << '\n'
              << "backend_initialization_input_us="
              << virtualStats.initializationInputUs << '\n'
              << "forward_latency_us_p50=" << forwardingLatency.percentile(50) << '\n'
              << "forward_latency_us_p95=" << forwardingLatency.percentile(95) << '\n'
              << "forward_latency_us_p99=" << forwardingLatency.percentile(99) << '\n'
              << "physical_report_rate_hz=" << std::fixed << std::setprecision(2)
              << physicalReportRateHz << '\n'
              << "virtual_report_rate_hz=" << virtualReportRateHz << '\n'
              << "cpu_percent_total=" << cpuPercent << std::defaultfloat << '\n'
              << "working_set_bytes=" << processUsageFinished.workingSetBytes << '\n'
              << "peak_working_set_bytes=" << processUsageFinished.peakWorkingSetBytes << '\n'
              << "input_reports_lost=" << lostInputReports << '\n'
              << "input_reports_coalesced=" << coalescedInputReports << '\n'
              << "input_keepalives=" << keepaliveInputReports << '\n'
              << "virtual_input_neutralized="
              << (virtualInputNeutralized ? "yes" : "no") << '\n'
              << "physical_controls_released_before_restore="
              << (physicalControlsReleased ? "yes" : "no") << '\n'
              << "apex_profile_requested="
              << (options.apexProfileSlot
                      ? std::to_string(*options.apexProfileSlot + 1)
                      : "none") << '\n'
              << "apex_profile_original="
              << (originalProfile
                      ? std::to_string(originalProfile->slot + 1)
                      : "unknown") << '\n'
              << "apex_profile_switch_acknowledged="
              << (profileSwitchAcknowledged ? "yes" : "no") << '\n'
              << "apex_profile_restored="
              << (profileRestored ? "yes" : "no") << '\n'
              << "input_updates=" << virtualStats.inputUpdates << '\n'
              << "dualsense_firmware_update="
              << (virtualFirmware ? hex16(virtualFirmware->updateVersion) : "unavailable")
              << '\n'
              << "dualsense_firmware_current="
              << (virtualFirmware && virtualFirmware->updateVersion >= 0x0630 ? "yes" : "no") << '\n'
              << "battery_percent="
              << static_cast<unsigned>(lastPhysicalInput ? lastPhysicalInput->batteryPercent : initialInput.batteryPercent) << '\n'
              << "charge_state="
              << static_cast<unsigned>(lastPhysicalInput ? lastPhysicalInput->chargeState : initialInput.chargeState)
              << '\n'
              << "dualsense_output_reports=" << virtualStats.outputReports << '\n'
              << "dualsense_trigger_reports=" << virtualStats.triggerReports << '\n'
              << "dualsense_rumble_reports=" << virtualStats.rumbleReports << '\n'
              << "dualsense_malformed_feedback_frames=" << virtualStats.malformedFrames << '\n'
              << "dualsense_unknown_feedback_frames=" << virtualStats.unknownFrames << '\n'
              << "audio_haptics_frames=" << virtualStats.audioHapticsFrames << '\n'
              << "audio_haptics_delivered=" << virtualStats.audioHapticsDelivered << '\n'
              << "audio_haptics_coalesced=" << virtualStats.audioHapticsCoalesced << '\n'
              << "audio_default_protection="
              << asb::platform::audioDefaultProtectionStatusName(audioProtection.status())
              << '\n'
              << "audio_default_roles_restored=" << audioProtection.restoredRoles() << '\n'
              << "input_samples=" << inputSamples << '\n'
              << "button_transitions=" << buttonTransitions << '\n'
              << "touchpad_click_presses=" << mappedTouchpadHold.presses() << '\n'
              << "maximum_touchpad_click_hold_ms="
              << mappedTouchpadHold.maximumHoldMilliseconds() << '\n'
              << "touchpad_gesture_profile="
              << asb::dualsense::touchpadGestureProfileName(options.touchpadProfile)
              << '\n'
              << "view_touchpad_gesture="
              << (options.touchpadProfile != asb::dualsense::TouchpadGestureProfile::None
                      ? "enabled" : "disabled") << '\n'
              << "view_touchpad_taps=" << touchpadGestureStats.replayedTaps << '\n'
              << "view_touchpad_swipes=" << touchpadGestureStats.swipes << '\n'
              << "touchpad_swipes_up=" << touchpadGestureStats.swipesByDirection[0] << '\n'
              << "touchpad_swipes_down=" << touchpadGestureStats.swipesByDirection[1] << '\n'
              << "touchpad_swipes_left=" << touchpadGestureStats.swipesByDirection[2] << '\n'
              << "touchpad_swipes_right=" << touchpadGestureStats.swipesByDirection[3] << '\n'
              << "seen_buttons=0x" << std::hex << std::uppercase << std::setw(4)
              << std::setfill('0') << seenButtons << std::dec << std::setfill(' ') << '\n'
              << "seen_dpad=0x" << std::hex << std::uppercase
              << static_cast<unsigned>(seenDpad) << std::dec << '\n'
              << "maximum_l2=" << static_cast<unsigned>(maximumL2) << '\n'
              << "maximum_r2=" << static_cast<unsigned>(maximumR2) << '\n'
              << "maximum_simultaneous_triggers="
              << static_cast<unsigned>(maximumSimultaneousTriggers) << '\n'
              << "simultaneous_trigger_reports="
              << simultaneousTriggerReports << '\n'
              << "right_stick_x_range="
              << static_cast<unsigned>(minimumRightStickX)
              << ',' << static_cast<unsigned>(maximumRightStickX) << '\n'
              << "right_stick_y_range="
              << static_cast<unsigned>(minimumRightStickY)
              << ',' << static_cast<unsigned>(maximumRightStickY) << '\n'
              << "virtual_input_monitor="
              << (virtualInputMonitor ? "enabled" : "disabled") << '\n'
              << "virtual_input_reports=" << virtualInputReports << '\n'
              << "virtual_seen_face=0x" << std::hex << std::uppercase
              << static_cast<unsigned>(virtualSeenFace) << std::dec << '\n'
              << "virtual_seen_shoulders=0x" << std::hex << std::uppercase
              << static_cast<unsigned>(virtualSeenShoulders) << std::dec << '\n'
              << "virtual_seen_system=0x" << std::hex << std::uppercase
              << static_cast<unsigned>(virtualSeenSystem) << std::dec << '\n'
              << "virtual_touchpad_click_presses=" << virtualTouchpadHold.presses() << '\n'
              << "virtual_maximum_touchpad_click_hold_ms="
              << virtualTouchpadHold.maximumHoldMilliseconds() << '\n'
              << "virtual_touch_starts=" << virtualTouchStarts << '\n'
              << "virtual_touch_active_reports=" << virtualTouchActiveReports << '\n'
              << "virtual_touch_movement_reports=" << virtualTouchMovementReports << '\n'
              << "virtual_touch_minimum_x="
              << (virtualTouchActiveReports == 0 ? 0 : virtualTouchMinimumX) << '\n'
              << "virtual_touch_maximum_x=" << virtualTouchMaximumX << '\n'
              << "virtual_touch_minimum_y="
              << (virtualTouchActiveReports == 0 ? 0 : virtualTouchMinimumY) << '\n'
              << "virtual_touch_maximum_y=" << virtualTouchMaximumY << '\n'
              << "virtual_seen_dpad_hats=0x" << std::hex << std::uppercase
              << virtualSeenDpadHats << std::dec << '\n'
              << "virtual_maximum_l2=" << static_cast<unsigned>(virtualMaximumL2) << '\n'
              << "virtual_maximum_r2=" << static_cast<unsigned>(virtualMaximumR2) << '\n'
              << "virtual_maximum_simultaneous_triggers="
              << static_cast<unsigned>(virtualMaximumSimultaneousTriggers) << '\n'
              << "virtual_simultaneous_trigger_reports="
              << virtualSimultaneousTriggerReports << '\n'
              << "virtual_right_stick_x_range="
              << (virtualInputReports == 0
                      ? 0 : static_cast<unsigned>(virtualMinimumRightStickX))
              << ',' << static_cast<unsigned>(virtualMaximumRightStickX) << '\n'
              << "virtual_right_stick_y_range="
              << (virtualInputReports == 0
                      ? 0 : static_cast<unsigned>(virtualMinimumRightStickY))
              << ',' << static_cast<unsigned>(virtualMaximumRightStickY) << '\n';
    if (!apex6Pro) {
        std::cout << "translated_effects=" << bridgeStats.translated << '\n'
                  << "active_effects=" << bridgeStats.active << '\n'
                  << "normal_effects=" << bridgeStats.normal << '\n'
                  << "deduplicated_effects=" << bridgeStats.deduplicated << '\n'
                  << "neutral_requests=" << bridgeStats.neutral << '\n'
                  << "unsupported_effects=" << bridgeStats.unsupported << '\n'
                  << "write_failures=" << bridgeStats.writeFailures << '\n';
    }
    if (apex6Pro) {
        std::cout << "apex6_hid_reports=" << apex6Stats.hidReports << '\n'
#ifdef ASB_RELEASE_LABEL
              << "asb_release=" ASB_RELEASE_LABEL "\n"
#endif
              << "apex6_trigger_left_updates=" << apex6Stats.triggerLeftUpdates << '\n'
              << "apex6_trigger_right_updates=" << apex6Stats.triggerRightUpdates << '\n'
              << "apex6_trigger_active_updates=" << apex6Stats.triggerActiveUpdates << '\n'
              << "apex6_trigger_stops=" << apex6Stats.triggerStops << '\n'
              << "apex6_trigger_unsupported=" << apex6Stats.triggerUnsupported << '\n'
              << "apex6_trigger_malformed=" << apex6Stats.triggerMalformed << '\n'
              << "apex6_trigger_rejected_stops=" << apex6Stats.triggerRejectedStops << '\n'
              << "apex6_trigger_deduplicated=" << apex6Stats.triggerDeduplicated << '\n'
              << "apex6_weapon_breaks=" << apex6Stats.weaponBreaks << '\n'
              << "apex6_bow_breaks=" << apex6Stats.bowBreaks << '\n'
              << "apex6_waveform_threshold_policy=native-pcm-preserved\n"
              << "apex6_pcm_gain_percent=" << apex6Stats.pcmGainPercent << '\n'
              << "apex6_haptic_threshold_percent=0\n"
              << "apex6_pcm_output_left_peak=" << apex6Stats.pcmOutputLeftPeak << '\n'
              << "apex6_pcm_output_right_peak=" << apex6Stats.pcmOutputRightPeak << '\n'
              << "apex6_last_left_trigger_type="
              << static_cast<unsigned>(apex6Stats.lastLeftTriggerType) << '\n'
              << "apex6_last_right_trigger_type="
              << static_cast<unsigned>(apex6Stats.lastRightTriggerType) << '\n'
              << "apex6_trigger_left_frames=" << apex6Stats.leftTriggerFrames << '\n'
              << "apex6_trigger_right_frames=" << apex6Stats.rightTriggerFrames << '\n'
              << "apex6_trigger_both_frames=" << apex6Stats.bothTriggerFrames << '\n'
              << "apex6_rumble_updates=" << apex6Stats.rumbleUpdates << '\n'
              << "apex6_rumble_active_updates=" << apex6Stats.rumbleActiveUpdates << '\n'
              << "apex6_audio_envelope_reports=" << apex6Stats.audioEnvelopeReports << '\n'
              << "apex6_audio_envelope_active=" << apex6Stats.audioEnvelopeActive << '\n'
              << "apex6_waveform_left_active=" << apex6Stats.waveformLeftActiveBlocks << '\n'
              << "apex6_waveform_right_active=" << apex6Stats.waveformRightActiveBlocks << '\n'
              << "apex6_waveform_left_peak=" << apex6Stats.waveformLeftPeak << '\n'
              << "apex6_waveform_right_peak=" << apex6Stats.waveformRightPeak << '\n'
              << "apex6_waveform_silent_blocks=" << apex6Stats.waveformSilentBlocks << '\n'
              << "apex6_waveform_left_thresholded=" << apex6Stats.waveformLeftThresholded << '\n'
              << "apex6_waveform_right_thresholded=" << apex6Stats.waveformRightThresholded << '\n'
              << "apex6_waveform_active_drops=" << apex6Stats.waveformActiveDrops << '\n'
              << "apex6_waveform_left_active_rms=" << apex6Stats.waveformLeftActiveRms << '\n'
              << "apex6_waveform_right_active_rms=" << apex6Stats.waveformRightActiveRms << '\n'
              << "apex6_raw_audio_measured_blocks=" << apex6Stats.rawAudioMeasuredBlocks << '\n'
              << "apex6_raw_audio_frames=" << apex6Stats.rawAudioFrames << '\n'
              << "apex6_raw_speaker_left_peak=" << apex6Stats.rawAudioPeaks[0] << '\n'
              << "apex6_raw_speaker_right_peak=" << apex6Stats.rawAudioPeaks[1] << '\n'
              << "apex6_raw_haptic_left_peak=" << apex6Stats.rawAudioPeaks[2] << '\n'
              << "apex6_raw_haptic_right_peak=" << apex6Stats.rawAudioPeaks[3] << '\n'
              << "apex6_raw_haptic_left_rms=" << apex6Stats.rawHapticLeftRms << '\n'
              << "apex6_raw_haptic_right_rms=" << apex6Stats.rawHapticRightRms << '\n'
              << "apex6_waveform_active_rendered=" << apex6Stats.waveformActiveRendered << '\n'
              << "apex6_grip_envelope_frames=" << apex6Stats.gripEnvelopeFrames << '\n'
              << "apex6_grip_rumble_frames=" << apex6Stats.gripRumbleFrames << '\n'
              << "apex6_haptic_enables=" << apex6Stats.hapticEnables << '\n'
              << "apex6_haptic_disables=" << apex6Stats.hapticDisables << '\n'
              << "apex6_haptic_frames=" << apex6Stats.framesWritten << '\n'
              << "apex6_waveform_blocks=" << apex6Stats.waveformBlocks << '\n'
              << "apex6_waveform_rendered=" << apex6Stats.waveformBlocksRendered << '\n'
              << "apex6_waveform_dropped=" << apex6Stats.waveformBlocksDropped << '\n'
              << "apex6_waveform_queue_max_depth=" << apex6Stats.waveformQueueMaxDepth << '\n'
              << "apex6_waveform_sequence_gaps=" << apex6Stats.waveformSequenceGaps << '\n'
              << "apex6_waveform_duplicates=" << apex6Stats.waveformDuplicates << '\n'
              << "apex6_waveform_out_of_order=" << apex6Stats.waveformOutOfOrder << '\n'
              << "apex6_waveform_underruns=" << apex6Stats.waveformUnderruns << '\n'
              << "apex6_waveform_overflow_drops=" << apex6Stats.waveformOverflowDrops << '\n'
              << "apex6_waveform_stale_drops=" << apex6Stats.waveformStaleDrops << '\n'
              << "apex6_waveform_average_age_us="
              << (apex6Stats.waveformBlocksRendered == 0 ? 0
                  : apex6Stats.waveformTotalAgeUs / apex6Stats.waveformBlocksRendered) << '\n'
              << "apex6_waveform_maximum_age_us="
              << apex6Stats.waveformMaximumAgeUs << '\n'
              << "apex6_deadline_overruns=" << apex6Stats.deadlineOverruns << '\n'
              << "apex6_idle_frames_skipped=" << apex6Stats.idleFramesSkipped << '\n'
              << "apex6_idle_keepalives=" << apex6Stats.idleKeepalives << '\n'
              << "apex6_silent_waveform_skipped=" << apex6Stats.silentWaveformSkipped << '\n'
              << "apex6_write_failures=" << apex6Stats.writeFailures << '\n'
              << "apex6_average_write_us="
              << (apex6Stats.framesWritten == 0
                      ? 0 : apex6Stats.totalWriteDurationUs / apex6Stats.framesWritten)
              << '\n'
              << "apex6_maximum_write_us=" << apex6Stats.maximumWriteDurationUs << '\n';
        writeApex6TriggerTrace(std::cout, apex6Stats);
    }
    std::cout << "rumble_routing="
              << ((rumbleBridge || (apex6Bridge && options.routeRumble))
                      ? "enabled" : "disabled") << '\n'
              << "audio_haptics_routing="
              << ((rumbleBridge || (apex6Bridge && options.routeRumble))
                      ? "enabled" : "disabled") << '\n';
    if (!apex6Pro) {
        std::cout << "rumble_updates=" << rumbleStats.updates << '\n'
              << "rumble_writes=" << rumbleStats.writes << '\n'
              << "rumble_stops=" << rumbleStats.stops << '\n'
              << "rumble_deduplicated=" << rumbleStats.deduplicated << '\n'
              << "rumble_write_failures=" << rumbleStats.writeFailures << '\n'
              << "last_rumble_low=" << static_cast<unsigned>(rumbleStats.lastLowFrequency) << '\n'
              << "last_rumble_high=" << static_cast<unsigned>(rumbleStats.lastHighFrequency) << '\n'
              << "audio_haptics_processed=" << rumbleStats.audioFrames << '\n'
              << "audio_haptics_active=" << rumbleStats.audioActiveFrames << '\n'
              << "audio_haptics_active_percent=" << std::fixed << std::setprecision(2)
              << (rumbleStats.audioFrames == 0
                      ? 0.0
                      : 100.0 * static_cast<double>(rumbleStats.audioActiveFrames) /
                            static_cast<double>(rumbleStats.audioFrames))
              << std::defaultfloat << '\n'
              << "audio_haptics_rate_limited=" << rumbleStats.audioRateLimited << '\n'
              << "audio_haptics_timeouts=" << rumbleStats.audioTimeouts << '\n'
              << "audio_haptics_low_frames=" << rumbleStats.audioLowFrames << '\n'
              << "audio_haptics_medium_frames=" << rumbleStats.audioMediumFrames << '\n'
              << "audio_haptics_high_frames=" << rumbleStats.audioHighFrames << '\n'
              << "audio_haptics_threshold_percent=" << options.hapticThresholdPercent << '\n'
              << "audio_max_left_energy=" << rumbleStats.maximumLeftEnergy << '\n'
              << "audio_max_right_energy=" << rumbleStats.maximumRightEnergy << '\n'
              << "audio_max_left_peak=" << rumbleStats.maximumLeftPeak << '\n'
              << "audio_max_right_peak=" << rumbleStats.maximumRightPeak << '\n'
              << "audio_max_left_transient=" << rumbleStats.maximumLeftTransient << '\n'
              << "audio_max_right_transient=" << rumbleStats.maximumRightTransient << '\n'
              << "last_audio_low="
              << static_cast<unsigned>(rumbleStats.lastAudioLowFrequency) << '\n'
              << "last_audio_high="
              << static_cast<unsigned>(rumbleStats.lastAudioHighFrequency) << '\n';
    }
    std::cout << "lightbar_routing=" << (lightbarBridge ? "enabled" : "disabled") << '\n';
    if (lightbarBridge) {
        std::cout << "lightbar_updates=" << lightbarStats.updates << '\n'
                  << "lightbar_writes=" << lightbarStats.writes << '\n'
                  << "lightbar_deduplicated=" << lightbarStats.deduplicated << '\n'
                  << "lightbar_write_failures=" << lightbarStats.writeFailures << '\n';
    }
    std::cout << "apex_original_restored="
              << (isolationRestored ? "yes" : "no") << '\n';
    std::cout << "apex_async_write_retries="
              << device->asyncWriteRetries() << '\n';
    const auto asyncStats = device->asyncWriteStats();
    std::cout << "apex_async_write_attempts=" << asyncStats.writes << '\n'
              << "apex_async_coalesced_updates=" << asyncStats.coalesced << '\n'
              << "apex_async_slow_writes=" << asyncStats.slowWrites << '\n'
              << "apex_async_queue_max_us=" << asyncStats.maximumQueueUs << '\n'
              << "apex_async_write_max_us=" << asyncStats.maximumWriteUs << '\n';
    const auto printLast = [](std::string_view side, std::uint8_t dsType,
                              const std::optional<asb::ForceTriggerCommand>& command) {
        std::cout << "last_" << side << "_ds_type=" << static_cast<unsigned>(dsType) << '\n';
        if (!command) {
            std::cout << "last_" << side << "_apex=none\n";
            return;
        }
        std::cout << "last_" << side << "_apex="
                  << static_cast<unsigned>(command->mode);
        for (const auto byte : command->params) std::cout << ',' << static_cast<unsigned>(byte);
        std::cout << '\n';
    };
    if (!apex6Pro) {
        printLast("lt", bridgeStats.lastLeftDualSenseType, bridgeStats.lastLeftCommand);
        printLast("rt", bridgeStats.lastRightDualSenseType, bridgeStats.lastRightCommand);
        printLast("active_lt", bridgeStats.lastActiveLeftDualSenseType,
                  bridgeStats.lastActiveLeftCommand);
        printLast("active_rt", bridgeStats.lastActiveRightDualSenseType,
                  bridgeStats.lastActiveRightCommand);
        asb::cli::writeAdaptiveTriggerDiagnostics(std::cout, bridgeStats);
    }
    if (!resetOk) {
        const std::string prefix = apex6Pro
            ? "Apex 6 haptic shutdown failed: "
            : "LT/RT automatic reset failed: ";
        std::cerr << "WARNING: " << prefix << resetError
                  << "\nPower-cycle the controller before continuing.\n";
        return failSession(5, prefix + resetError);
    }
    if (!rumbleResetOk) {
        std::cerr << "WARNING: grip-rumble automatic stop failed: "
                  << rumbleResetError << "\nPower-cycle the controller before continuing.\n";
        return failSession(12, "Grip-rumble automatic stop failed: " + rumbleResetError);
    }
    if (!profileRestored) {
        const std::string message =
            "Could not restore the original Apex 5 profile: " + profileRestoreError;
        std::cerr << "WARNING: " << message << "\nUse the controller's profile "
                     "shortcut to restore it manually.\n";
        return failSession(15, message);
    }
    if (!isolationRestored) {
        std::cerr << "WARNING: could not restore the original APEX session state: "
                  << isolationRestoreError
                  << "\nRun 'ApexSenseBridge restore-controller-visibility' before playing without the bridge.\n";
        return failSession(11, "Could not restore the original APEX session state: " +
                                   isolationRestoreError);
    }
    if (adaptiveBridge && adaptiveBridge->failed()) {
        const std::string message =
            "Bridge stopped after an APEX write failure: " + adaptiveBridge->error();
        std::cerr << message << '\n';
        return failSession(4, message);
    }
    if (apex6Bridge && apex6Bridge->failed()) {
        const std::string message =
            "Bridge stopped after an Apex 6 haptic write failure: " +
            apex6Bridge->error();
        std::cerr << message << '\n';
        return failSession(4, message);
    }
    if (rumbleBridge && rumbleBridge->failed()) {
        std::cerr << "Bridge stopped after an APEX rumble write failure: "
                  << rumbleBridge->error() << '\n';
        return failSession(12, "Bridge stopped after an APEX rumble write failure: " +
                                   rumbleBridge->error());
    }
    if (!asyncWriteError.empty()) {
        const std::string message =
            "Bridge stopped after repeated APEX feedback write failures: " +
            asyncWriteError;
        std::cerr << message << '\n';
        return failSession(4, message);
    }
    if (inputProxyFailed) {
        const std::string message =
            "Bridge stopped after a mandatory physical-input proxy failure: " +
            inputProxyError;
        std::cerr << message << '\n';
        return failSession(8, message);
    }
    if (disconnected) {
        constexpr std::string_view message = "The VIIPER feedback stream disconnected unexpectedly.";
        std::cerr << message << '\n';
        return failSession(7, message);
    }
    if (sessionControl &&
        !sessionControl->publish(asb::platform::SessionPhase::Stopped, 0,
                                 "Bridge stopped and controller state restored.", error)) {
        std::cerr << "Bridge session completion status failed: " << error << '\n';
        return 13;
    }
    std::cout << (apex6Pro
                      ? "Apex 6 trigger and grip voice coils stopped.\n"
                      : "LT and RT reset to Normal; grip rumble stopped.\n");
    return 0;
}

} // namespace asb::cli
