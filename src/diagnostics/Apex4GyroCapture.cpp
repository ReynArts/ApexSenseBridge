#include "diagnostics/Apex4GyroCapture.h"

#include "flydigi/Apex4Protocol.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace asb::diagnostics {
namespace {

std::string jsonEscape(std::string_view value) {
    std::ostringstream output;
    for (const unsigned char byte : value) {
        switch (byte) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (byte < 0x20) {
                output << "\\u" << std::hex << std::uppercase
                       << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(byte) << std::dec;
            } else {
                output << static_cast<char>(byte);
            }
            break;
        }
    }
    return output.str();
}

std::string hexBytes(std::span<const std::uint8_t> bytes) {
    std::ostringstream output;
    output << std::hex << std::uppercase << std::setfill('0');
    for (const auto byte : bytes) {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return output.str();
}

std::string hex16(std::uint16_t value) {
    std::ostringstream output;
    output << std::hex << std::uppercase << std::setw(4)
           << std::setfill('0') << value;
    return output.str();
}

} // namespace

Apex4MotionPhaseCapture::Apex4MotionPhaseCapture(
    std::string name, std::string instruction, std::size_t maximumStoredSamples)
    : name_(std::move(name)), instruction_(std::move(instruction)),
      maximumStoredSamples_(maximumStoredSamples) {
    minimum_.fill(0xFF);
}

bool Apex4MotionPhaseCapture::addReport(
    std::span<const std::uint8_t> report,
    std::uint64_t elapsedMicroseconds) {
    ++observedReports_;
    if (report.size() < 2 ||
        report[0] != flydigi::kApex4InputReportId ||
        report[1] != flydigi::kApex4StateMarker) {
        ++rejectedReports_;
        return false;
    }
    if (report.size() < kApex4MinimumStateReportSize) {
        ++shortReports_;
        return false;
    }

    maximumObservedReportSize_ = (std::max)(maximumObservedReportSize_, report.size());
    const auto capturedSize = (std::min)(report.size(), kApex4MaximumCapturedReportSize);
    if (capturedSize != report.size()) ++truncatedReports_;
    const auto captured = report.first(capturedSize);

    for (std::size_t offset = 0; offset < captured.size(); ++offset) {
        if (isKnownApex4MotionOffset(offset) && captured[offset] != 0) {
            motionDataObserved_ = true;
        }
    }

    for (std::size_t offset = 0; offset < captured.size(); ++offset) {
        const auto value = captured[offset];
        minimum_[offset] = (std::min)(minimum_[offset], value);
        maximum_[offset] = (std::max)(maximum_[offset], value);
        unique_[offset].set(value);
        if (initialized_ && offset < previous_.size() && previous_[offset] != value) {
            ++changes_[offset];
        }
    }
    previous_.assign(captured.begin(), captured.end());
    initialized_ = true;

    if (samples_.size() < maximumStoredSamples_) {
        samples_.push_back(Apex4MotionSample{
            elapsedMicroseconds,
            std::vector<std::uint8_t>(captured.begin(), captured.end())});
    } else {
        ++droppedSamples_;
    }
    return true;
}

const std::string& Apex4MotionPhaseCapture::name() const noexcept { return name_; }
const std::string& Apex4MotionPhaseCapture::instruction() const noexcept {
    return instruction_;
}
const std::vector<Apex4MotionSample>&
Apex4MotionPhaseCapture::samples() const noexcept {
    return samples_;
}

std::vector<Apex4MotionByteActivity>
Apex4MotionPhaseCapture::byteActivity() const {
    std::vector<Apex4MotionByteActivity> result;
    for (std::size_t offset = 0; offset < kApex4MaximumCapturedReportSize; ++offset) {
        const auto uniqueValues = unique_[offset].count();
        if (uniqueValues < 2) continue;
        result.push_back(Apex4MotionByteActivity{
            offset,
            minimum_[offset],
            maximum_[offset],
            changes_[offset],
            uniqueValues,
            isKnownApex4ControlOffset(offset)});
    }
    return result;
}

std::uint64_t Apex4MotionPhaseCapture::observedReports() const noexcept {
    return observedReports_;
}
std::uint64_t Apex4MotionPhaseCapture::rejectedReports() const noexcept {
    return rejectedReports_;
}
std::uint64_t Apex4MotionPhaseCapture::shortReports() const noexcept {
    return shortReports_;
}
std::uint64_t Apex4MotionPhaseCapture::truncatedReports() const noexcept {
    return truncatedReports_;
}
std::uint64_t Apex4MotionPhaseCapture::droppedSamples() const noexcept {
    return droppedSamples_;
}
std::size_t Apex4MotionPhaseCapture::maximumObservedReportSize() const noexcept {
    return maximumObservedReportSize_;
}
bool Apex4MotionPhaseCapture::motionDataObserved() const noexcept {
    return motionDataObserved_;
}

bool isKnownApex4ControlOffset(std::size_t offset) noexcept {
    // Header, Guide/buttons, sticks and triggers decoded by Apex4Input.cpp.
    constexpr std::array<std::size_t, 11> known{
        0, 1, 8, 9, 10, 17, 19, 21, 22, 23, 24};
    return std::find(known.begin(), known.end(), offset) != known.end();
}

bool isKnownApex4MotionOffset(std::size_t offset) noexcept {
    // Mouse mapping channels, accelerometer, split yaw, pitch and roll.
    constexpr std::array<std::size_t, 15> known{
        4, 5, 6, 11, 12, 13, 14, 15, 16, 18, 20, 26, 27, 29, 30};
    return std::find(known.begin(), known.end(), offset) != known.end();
}

std::string formatApex4GyroCaptureJson(
    const Apex4GyroCaptureMetadata& metadata,
    const std::vector<Apex4MotionPhaseCapture>& phases) {
    std::ostringstream output;
    output << "{\n"
           << "  \"schema\": 1,\n"
           << "  \"tool\": \"ApexSenseBridge apex4-gyro-capture\",\n"
           << "  \"privacy\": \"Raw controller HID input only; no keyboard, mouse, process, path, or personal data.\",\n"
           << "  \"model\": \"" << jsonEscape(metadata.model) << "\",\n"
           << "  \"connection\": \"" << jsonEscape(metadata.connection) << "\",\n"
           << "  \"connection_raw\": "
           << static_cast<unsigned int>(metadata.connectionRaw) << ",\n"
           << "  \"vendor_id\": \"" << hex16(metadata.vendorId) << "\",\n"
           << "  \"product_id\": \"" << hex16(metadata.productId) << "\",\n"
           << "  \"declared_input_report_length\": "
           << metadata.declaredInputReportLength << ",\n"
           << "  \"phase_seconds\": " << metadata.phaseSeconds << ",\n"
           << "  \"interrupted\": " << (metadata.interrupted ? "true" : "false") << ",\n"
           << "  \"known_control_offsets\": [0, 1, 8, 9, 10, 17, 19, 21, 22, 23, 24],\n"
           << "  \"known_motion_offsets\": [4, 5, 6, 11, 12, 13, 14, 15, 16, 18, 20, 26, 27, 29, 30],\n"
           << "  \"phases\": [\n";

    for (std::size_t phaseIndex = 0; phaseIndex < phases.size(); ++phaseIndex) {
        const auto& phase = phases[phaseIndex];
        const auto activity = phase.byteActivity();
        output << "    {\n"
               << "      \"name\": \"" << jsonEscape(phase.name()) << "\",\n"
               << "      \"instruction\": \""
               << jsonEscape(phase.instruction()) << "\",\n"
               << "      \"observed_reports\": " << phase.observedReports() << ",\n"
               << "      \"stored_state_reports\": " << phase.samples().size() << ",\n"
               << "      \"rejected_non_state_reports\": "
               << phase.rejectedReports() << ",\n"
               << "      \"short_state_reports\": " << phase.shortReports() << ",\n"
               << "      \"truncated_reports\": " << phase.truncatedReports() << ",\n"
               << "      \"dropped_samples\": " << phase.droppedSamples() << ",\n"
               << "      \"maximum_observed_report_length\": "
               << phase.maximumObservedReportSize() << ",\n"
               << "      \"motion_data_observed\": "
               << (phase.motionDataObserved() ? "true" : "false") << ",\n"
               << "      \"byte_activity\": [";
        for (std::size_t activityIndex = 0;
             activityIndex < activity.size(); ++activityIndex) {
            const auto& item = activity[activityIndex];
            if (activityIndex != 0) output << ',';
            output << "\n        {\"offset\": " << item.offset
                   << ", \"min\": " << static_cast<unsigned int>(item.minimum)
                   << ", \"max\": " << static_cast<unsigned int>(item.maximum)
                   << ", \"changes\": " << item.changes
                   << ", \"unique_values\": " << item.uniqueValues
                   << ", \"known_control\": "
                   << (item.knownControl ? "true" : "false") << '}';
        }
        if (!activity.empty()) output << '\n' << "      ";
        output << "],\n      \"samples\": [";
        const auto& samples = phase.samples();
        for (std::size_t sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
            const auto& sample = samples[sampleIndex];
            if (sampleIndex != 0) output << ',';
            output << "\n        {\"t_us\": " << sample.elapsedMicroseconds
                   << ", \"hex\": \"" << hexBytes(sample.report) << "\"}";
        }
        if (!samples.empty()) output << '\n' << "      ";
        output << "]\n    }";
        if (phaseIndex + 1 != phases.size()) output << ',';
        output << '\n';
    }
    output << "  ]\n}\n";
    return output.str();
}

} // namespace asb::diagnostics
