#include "dualsense/VirtualDualSenseStartup.h"

#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

namespace asb::dualsense {
namespace {

constexpr std::size_t kMaximumReportedInterfaces = 8;

bool sameDevicePath(std::wstring_view left, std::wstring_view right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(),
                      [](wchar_t lhs, wchar_t rhs) {
                          return std::towupper(lhs) == std::towupper(rhs);
                      });
}

bool containsPath(const std::vector<std::wstring>& paths, std::wstring_view candidate) {
    return std::any_of(paths.begin(), paths.end(), [candidate](const auto& path) {
        return sameDevicePath(path, candidate);
    });
}

bool listedIn(const std::vector<HidDeviceInfo>& devices, std::wstring_view path) {
    return std::any_of(devices.begin(), devices.end(), [path](const auto& info) {
        return sameDevicePath(info.path, path);
    });
}

std::string asciiText(std::wstring_view value) {
    std::string out;
    out.reserve(value.size());
    for (const wchar_t ch : value) {
        out.push_back(ch >= 32 && ch <= 126 ? static_cast<char>(ch) : '?');
    }
    return out;
}

std::string hex4(std::uint16_t value) {
    std::ostringstream text;
    text << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << value;
    return text.str();
}

std::int64_t millisecondsBetween(std::chrono::steady_clock::time_point from,
                                 std::chrono::steady_clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
}

struct ProbeClock {
    const VirtualDualSenseProbeEnvironment& environment;

    [[nodiscard]] std::chrono::steady_clock::time_point now() const {
        return environment.now ? environment.now() : std::chrono::steady_clock::now();
    }

    void sleep(std::chrono::milliseconds duration) const {
        if (duration <= std::chrono::milliseconds(0)) return;
        if (environment.sleep) {
            environment.sleep(duration);
        } else {
            std::this_thread::sleep_for(duration);
        }
    }

    [[nodiscard]] bool stopRequested() const {
        return environment.stopRequested && environment.stopRequested();
    }
};

struct SeenInterface {
    std::wstring path;
    std::size_t reportIndex = static_cast<std::size_t>(-1);
    bool countedGamepad = false;
    bool countedUnreadable = false;
    bool countedOther = false;
    bool explained = false;
};

constexpr std::string_view kCancelledProbe =
    "Virtual DualSense readiness verification was cancelled by a stop request.";

} // namespace

bool isDualSenseHidInterface(const HidDeviceInfo& info) noexcept {
    return info.vendorId == 0x054C && info.productId == 0x0CE6;
}

bool isDualSenseGamepadCollection(const HidDeviceInfo& info) noexcept {
    return isDualSenseHidInterface(info) &&
           (info.interfaceNumber.empty() || info.interfaceNumber == L"MI_03") &&
           info.featureReportLength >= 46;
}

VirtualDualSenseBaseline::VirtualDualSenseBaseline(std::vector<std::wstring> paths) {
    reset(std::move(paths));
}

void VirtualDualSenseBaseline::reset(std::vector<std::wstring> paths) {
    initial_.clear();
    for (auto& path : paths) {
        if (!containsPath(initial_, path)) initial_.push_back(std::move(path));
    }
    active_ = initial_;
    vanished_.clear();
    reappeared_.clear();
}

void VirtualDualSenseBaseline::observe(const std::vector<HidDeviceInfo>& devices) {
    for (auto it = active_.begin(); it != active_.end();) {
        if (!listedIn(devices, *it)) {
            if (!containsPath(vanished_, *it)) vanished_.push_back(*it);
            it = active_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& info : devices) {
        if (containsPath(vanished_, info.path) && !containsPath(reappeared_, info.path)) {
            reappeared_.push_back(info.path);
        }
    }
}

bool VirtualDualSenseBaseline::contains(std::wstring_view path) const {
    return containsPath(active_, path);
}

bool VirtualDualSenseBaseline::wasPresentInitially(std::wstring_view path) const {
    return containsPath(initial_, path);
}

std::string_view virtualDualSenseProbeOutcomeName(VirtualDualSenseProbeOutcome outcome) noexcept {
    switch (outcome) {
    case VirtualDualSenseProbeOutcome::Ready: return "ready";
    case VirtualDualSenseProbeOutcome::NotFound: return "not-found";
    case VirtualDualSenseProbeOutcome::CapabilitiesUnavailable: return "capabilities-unavailable";
    case VirtualDualSenseProbeOutcome::UnexpectedInterface: return "unexpected-interface";
    case VirtualDualSenseProbeOutcome::CandidateRejected: return "candidate-rejected";
    case VirtualDualSenseProbeOutcome::EnumerationFailed: return "enumeration-failed";
    case VirtualDualSenseProbeOutcome::Cancelled: return "cancelled";
    }
    return "unknown";
}

std::string describeVirtualDualSenseProbe(const VirtualDualSenseProbeReport& report) {
    std::ostringstream text;
    text << "attempt=" << report.attempt
         << " outcome=" << virtualDualSenseProbeOutcomeName(report.outcome)
         << " elapsed_ms=" << report.elapsedMilliseconds
         << " deadline_ms=" << report.deadlineMilliseconds
         << " extended=" << (report.extended ? "yes" : "no")
         << " first_new_ms=" << report.firstNewInterfaceMilliseconds
         << " enumerations=" << report.enumerations
         << " enumeration_failures=" << report.enumerationFailures
         << " baseline=" << report.baselineInterfaces
         << " baseline_vanished=" << report.baselineVanished
         << " baseline_reappeared=" << report.baselineReappeared
         << " new_gamepad=" << report.newGamepadInterfaces
         << " new_unreadable=" << report.newUnreadableInterfaces
         << " new_other=" << report.newOtherInterfaces
         << " candidate_failures=" << report.candidateFailures
         << " interfaces=[";
    for (std::size_t index = 0; index < report.interfaces.size(); ++index) {
        const auto& entry = report.interfaces[index];
        if (index != 0) text << "; ";
        text << entry.state
             << " mi=" << (entry.interfaceNumber.empty() ? "-" : entry.interfaceNumber)
             << " usage=" << hex4(entry.usagePage) << ':' << hex4(entry.usage)
             << " in=" << entry.inputReportLength
             << " feature=" << entry.featureReportLength
             << " first_ms=" << entry.firstSeenMilliseconds;
        if (!entry.detail.empty()) text << " detail=\"" << entry.detail << '"';
        text << " path=" << entry.path;
    }
    text << ']';
    if (!report.lastError.empty()) text << " last_error=\"" << report.lastError << '"';
    return text.str();
}

std::optional<VirtualDualSenseReadiness> probeNewVirtualDualSense(
    VirtualDualSenseBaseline& baseline,
    const VirtualDualSenseProbeTiming& timing,
    const VirtualDualSenseProbeEnvironment& environment,
    VirtualDualSenseProbeReport& report,
    std::string& error) {
    const ProbeClock clock{environment};
    const auto attempt = report.attempt;
    report = {};
    report.attempt = attempt;
    report.baselineInterfaces = baseline.activeCount();

    const auto quietWindow = (std::max)(timing.quietWindow, std::chrono::milliseconds(0));
    const auto progressWindow = (std::max)(timing.progressWindow, quietWindow);
    const auto pollInterval = (std::max)(timing.pollInterval, std::chrono::milliseconds(1));
    const auto startedAt = clock.now();
    auto deadline = startedAt + quietWindow;
    const auto vanishedAtStart = baseline.vanishedCount();
    const auto extend = [&] {
        if (report.extended) return;
        report.extended = true;
        deadline = startedAt + progressWindow;
    };

    std::vector<SeenInterface> seen;
    std::string candidateError;
    std::string enumerationError;
    std::string unreadableDetail;
    bool cancelled = false;

    for (;;) {
        if (clock.stopRequested()) {
            cancelled = true;
            break;
        }
        std::string scanError;
        auto devices = environment.enumerate
            ? environment.enumerate(scanError)
            : std::vector<HidDeviceInfo>{};
        ++report.enumerations;
        const auto elapsed = millisecondsBetween(startedAt, clock.now());
        if (!scanError.empty()) {
            ++report.enumerationFailures;
            enumerationError = std::move(scanError);
        } else {
            baseline.observe(devices);
        }
        if (baseline.vanishedCount() > vanishedAtStart) extend();

        for (auto& entry : seen) {
            if (entry.reportIndex < report.interfaces.size() &&
                report.interfaces[entry.reportIndex].state == "baseline" &&
                !baseline.contains(entry.path)) {
                report.interfaces[entry.reportIndex].state = "baseline-vanished";
            }
        }

        for (const auto& info : devices) {
            if (!isDualSenseHidInterface(info)) continue;
            auto found = std::find_if(seen.begin(), seen.end(), [&info](const auto& entry) {
                return sameDevicePath(entry.path, info.path);
            });
            if (found == seen.end()) {
                SeenInterface entry{};
                entry.path = info.path;
                if (report.interfaces.size() < kMaximumReportedInterfaces) {
                    entry.reportIndex = report.interfaces.size();
                    VirtualDualSenseProbeInterface reported{};
                    reported.path = asciiText(info.path);
                    reported.firstSeenMilliseconds = elapsed;
                    report.interfaces.push_back(std::move(reported));
                }
                seen.push_back(std::move(entry));
                found = seen.end() - 1;
            }
            VirtualDualSenseProbeInterface scratch{};
            auto& reported = found->reportIndex < report.interfaces.size()
                ? report.interfaces[found->reportIndex]
                : scratch;
            reported.interfaceNumber = asciiText(info.interfaceNumber);
            reported.usagePage = info.usagePage;
            reported.usage = info.usage;
            reported.inputReportLength = info.inputReportLength;
            reported.featureReportLength = info.featureReportLength;

            if (baseline.contains(info.path)) {
                reported.state = "baseline";
                continue;
            }

            if (report.firstNewInterfaceMilliseconds < 0) {
                report.firstNewInterfaceMilliseconds = elapsed;
            }
            extend();
            const bool reappeared = baseline.wasPresentInitially(info.path);

            if (isDualSenseGamepadCollection(info)) {
                reported.state = reappeared ? "reappeared-gamepad" : "new-gamepad";
                if (!found->countedGamepad) {
                    found->countedGamepad = true;
                    ++report.newGamepadInterfaces;
                }
                std::string verifyError;
                auto readiness = environment.verify
                    ? environment.verify(info, verifyError)
                    : std::optional<VirtualDualSenseReadiness>{};
                if (readiness) {
                    reported.state = "ready";
                    reported.detail.clear();
                    report.outcome = VirtualDualSenseProbeOutcome::Ready;
                    report.elapsedMilliseconds = millisecondsBetween(startedAt, clock.now());
                    report.deadlineMilliseconds = millisecondsBetween(startedAt, deadline);
                    report.baselineVanished = baseline.vanishedCount();
                    report.baselineReappeared = baseline.reappearedCount();
                    report.lastError.clear();
                    if (!readiness->deviceInfo) readiness->deviceInfo = info;
                    error.clear();
                    return readiness;
                }
                ++report.candidateFailures;
                candidateError = verifyError.empty()
                    ? "The virtual DualSense HID interface rejected readiness verification."
                    : std::move(verifyError);
                reported.detail = candidateError;
            } else if (info.featureReportLength == 0 && info.inputReportLength == 0) {
                reported.state = reappeared ? "reappeared-unreadable" : "new-unreadable";
                if (!found->countedUnreadable) {
                    found->countedUnreadable = true;
                    ++report.newUnreadableInterfaces;
                }
                if (!found->explained && environment.explainUnreadable) {
                    found->explained = true;
                    reported.detail = environment.explainUnreadable(info);
                    if (!reported.detail.empty()) unreadableDetail = reported.detail;
                }
            } else {
                reported.state = reappeared ? "reappeared-other" : "new-other";
                if (!found->countedOther) {
                    found->countedOther = true;
                    ++report.newOtherInterfaces;
                }
            }
        }

        const auto now = clock.now();
        if (now >= deadline) break;
        clock.sleep((std::min)(
            pollInterval,
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now) +
                std::chrono::milliseconds(1)));
    }

    report.elapsedMilliseconds = millisecondsBetween(startedAt, clock.now());
    report.deadlineMilliseconds = millisecondsBetween(startedAt, deadline);
    report.baselineVanished = baseline.vanishedCount();
    report.baselineReappeared = baseline.reappearedCount();

    if (cancelled) {
        report.outcome = VirtualDualSenseProbeOutcome::Cancelled;
        error = std::string(kCancelledProbe);
    } else if (report.candidateFailures != 0) {
        report.outcome = VirtualDualSenseProbeOutcome::CandidateRejected;
        error = candidateError;
    } else if (report.newUnreadableInterfaces != 0) {
        report.outcome = VirtualDualSenseProbeOutcome::CapabilitiesUnavailable;
        error = "A new virtual DualSense HID interface appeared, but Windows did not expose "
                "its HID capabilities";
        error += unreadableDetail.empty() ? std::string(".") : " (" + unreadableDetail + ").";
    } else if (report.newOtherInterfaces != 0) {
        report.outcome = VirtualDualSenseProbeOutcome::UnexpectedInterface;
        error = "A new 054C:0CE6 HID interface appeared, but none was the DualSense gamepad "
                "collection (MI_03 with feature reports of at least 46 bytes).";
    } else if (report.enumerationFailures != 0 && !enumerationError.empty()) {
        report.outcome = VirtualDualSenseProbeOutcome::EnumerationFailed;
        error = enumerationError;
    } else {
        report.outcome = VirtualDualSenseProbeOutcome::NotFound;
        error = "The newly-created virtual DualSense firmware interface was not found "
                "(no new 054C:0CE6 HID interface appeared";
        if (report.baselineInterfaces != 0) {
            error += "; " + std::to_string(report.baselineInterfaces) +
                     (report.baselineInterfaces == 1
                          ? " DualSense HID interface was"
                          : " DualSense HID interfaces were") +
                     " already present before creation";
        }
        error += ").";
    }
    report.lastError = error;
    return std::nullopt;
}

bool waitForVirtualDualSenseRemoval(
    VirtualDualSenseBaseline& baseline,
    const VirtualDualSenseProbeEnvironment& environment,
    std::chrono::milliseconds timeout,
    std::chrono::milliseconds stableAbsence,
    std::string& error) {
    const ProbeClock clock{environment};
    const auto startedAt = clock.now();
    const auto deadline = startedAt + (std::max)(timeout, std::chrono::milliseconds(0));
    std::optional<std::chrono::steady_clock::time_point> absentSince;
    std::string lastError;
    do {
        if (clock.stopRequested()) {
            error = "Waiting for the failed virtual DualSense removal was cancelled by a stop request.";
            return false;
        }
        std::string scanError;
        const auto devices = environment.enumerate
            ? environment.enumerate(scanError)
            : std::vector<HidDeviceInfo>{};
        if (!scanError.empty()) {
            lastError = std::move(scanError);
            absentSince.reset();
        } else {
            baseline.observe(devices);
            const bool newInterfacePresent = std::any_of(
                devices.begin(), devices.end(), [&baseline](const auto& info) {
                    return isDualSenseHidInterface(info) && !baseline.contains(info.path);
                });
            const auto now = clock.now();
            if (!newInterfacePresent) {
                if (!absentSince) absentSince = now;
                if (now - *absentSince >= stableAbsence) {
                    error.clear();
                    return true;
                }
            } else {
                absentSince.reset();
            }
        }
        clock.sleep(std::chrono::milliseconds(50));
    } while (clock.now() < deadline);

    error = lastError.empty()
        ? "The previous virtual DualSense HID interface remained present after cleanup."
        : std::move(lastError);
    return false;
}

bool startVerifiedVirtualDualSense(
    VirtualDualSense& backend,
    const DualSenseInputState& initialInput,
    VirtualDualSense::FeedbackHandler feedbackHandler,
    const VirtualDualSenseFirmwareProbe& probeFirmware,
    const VirtualDualSenseRemovalWait& waitForRemoval,
    std::size_t maximumAttempts,
    VirtualDualSenseStartupResult& result,
    std::string& error) {
    return startVerifiedVirtualDualSense(
        backend, initialInput, std::move(feedbackHandler), probeFirmware, waitForRemoval,
        maximumAttempts, result, error, VirtualDualSenseStartupHooks{});
}

bool startVerifiedVirtualDualSense(
    VirtualDualSense& backend,
    const DualSenseInputState& initialInput,
    VirtualDualSense::FeedbackHandler feedbackHandler,
    const VirtualDualSenseFirmwareProbe& probeFirmware,
    const VirtualDualSenseRemovalWait& waitForRemoval,
    std::size_t maximumAttempts,
    VirtualDualSenseStartupResult& result,
    std::string& error,
    const VirtualDualSenseStartupHooks& hooks) {
    result = {};
    error.clear();
    if (maximumAttempts == 0 || !probeFirmware ||
        (maximumAttempts > 1 && !waitForRemoval)) {
        backend.close();
        result.failure = VirtualDualSenseStartupFailure::ReadinessVerification;
        error = maximumAttempts == 0
            ? "Virtual DualSense startup requires at least one attempt."
            : "Virtual DualSense startup is missing a required readiness callback.";
        return false;
    }

    const auto stopRequested = [&hooks] {
        return hooks.stopRequested && hooks.stopRequested();
    };
    const auto cancel = [&] {
        backend.close();
        result.inputReadyAt = {};
        result.verifiedAt = {};
        result.deviceInfo.reset();
        result.firmware.reset();
        result.initialReportsValidated = 0;
        result.failure = VirtualDualSenseStartupFailure::Cancelled;
        error = "Virtual DualSense startup was cancelled by a stop request.";
        return false;
    };

    std::string lastVerificationError;
    for (std::size_t attempt = 1; attempt <= maximumAttempts; ++attempt) {
        if (stopRequested()) return cancel();
        result.attempts = attempt;
        if (hooks.beforeAttempt) hooks.beforeAttempt(attempt);

        std::string stageError;
        if (!backend.open(stageError, feedbackHandler)) {
            backend.close();
            result.failure = VirtualDualSenseStartupFailure::BackendOpen;
            error = "Virtual DualSense creation failed";
            if (!stageError.empty()) error += ": " + stageError;
            return false;
        }

        stageError.clear();
        if (!backend.updateInput(initialInput, stageError)) {
            backend.close();
            result.failure = VirtualDualSenseStartupFailure::InitialInput;
            error = "Initial physical-to-DualSense input forwarding failed";
            if (!stageError.empty()) error += ": " + stageError;
            return false;
        }
        result.inputReadyAt = std::chrono::steady_clock::now();

        stageError.clear();
        if (auto readiness = probeFirmware(stageError)) {
            result.firmware = static_cast<const DualSenseFirmwareInfo&>(*readiness);
            result.deviceInfo = std::move(readiness->deviceInfo);
            result.initialReportsValidated = readiness->initialReportsValidated;
            result.verifiedAt = std::chrono::steady_clock::now();
            result.failure = VirtualDualSenseStartupFailure::None;
            error.clear();
            return true;
        }
        if (stopRequested()) return cancel();
        lastVerificationError = stageError.empty()
            ? "The virtual DualSense HID firmware interface did not become ready."
            : std::move(stageError);
        backend.close();

        if (attempt == maximumAttempts) break;

        stageError.clear();
        if (!waitForRemoval(stageError)) {
            if (stopRequested()) return cancel();
            result.inputReadyAt = {};
            result.deviceInfo.reset();
            result.initialReportsValidated = 0;
            result.failure = VirtualDualSenseStartupFailure::DeviceRemoval;
            error = "The failed virtual DualSense could not be removed before retry";
            if (!stageError.empty()) error += ": " + stageError;
            return false;
        }
    }

    result.inputReadyAt = {};
    result.verifiedAt = {};
    result.deviceInfo.reset();
    result.initialReportsValidated = 0;
    result.failure = VirtualDualSenseStartupFailure::ReadinessVerification;
    error = "Virtual DualSense HID readiness verification failed after " +
            std::to_string(result.attempts) +
            (result.attempts == 1 ? " attempt" : " attempts") + ": " +
            lastVerificationError;
    return false;
}

} // namespace asb::dualsense
