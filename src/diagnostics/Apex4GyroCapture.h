#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace asb::diagnostics {

inline constexpr std::size_t kApex4MinimumStateReportSize = 32;
inline constexpr std::size_t kApex4MaximumCapturedReportSize = 128;

struct Apex4MotionSample {
    std::uint64_t elapsedMicroseconds = 0;
    std::vector<std::uint8_t> report;
};

struct Apex4MotionByteActivity {
    std::size_t offset = 0;
    std::uint8_t minimum = 0;
    std::uint8_t maximum = 0;
    std::uint64_t changes = 0;
    std::size_t uniqueValues = 0;
    bool knownControl = false;
};

class Apex4MotionPhaseCapture {
public:
    Apex4MotionPhaseCapture(std::string name,
                            std::string instruction,
                            std::size_t maximumStoredSamples = 100000);

    // Accepts only the Apex 4's 04 FE state reports. Other traffic can share
    // the interface after identity probing and is counted but never stored.
    bool addReport(std::span<const std::uint8_t> report,
                   std::uint64_t elapsedMicroseconds);

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const std::string& instruction() const noexcept;
    [[nodiscard]] const std::vector<Apex4MotionSample>& samples() const noexcept;
    [[nodiscard]] std::vector<Apex4MotionByteActivity> byteActivity() const;
    [[nodiscard]] std::uint64_t observedReports() const noexcept;
    [[nodiscard]] std::uint64_t rejectedReports() const noexcept;
    [[nodiscard]] std::uint64_t shortReports() const noexcept;
    [[nodiscard]] std::uint64_t truncatedReports() const noexcept;
    [[nodiscard]] std::uint64_t droppedSamples() const noexcept;
    [[nodiscard]] std::size_t maximumObservedReportSize() const noexcept;

private:
    std::string name_;
    std::string instruction_;
    std::size_t maximumStoredSamples_ = 0;
    std::vector<Apex4MotionSample> samples_;
    std::vector<std::uint8_t> previous_;
    std::array<std::uint8_t, kApex4MaximumCapturedReportSize> minimum_{};
    std::array<std::uint8_t, kApex4MaximumCapturedReportSize> maximum_{};
    std::array<std::uint64_t, kApex4MaximumCapturedReportSize> changes_{};
    std::array<std::bitset<256>, kApex4MaximumCapturedReportSize> unique_{};
    std::uint64_t observedReports_ = 0;
    std::uint64_t rejectedReports_ = 0;
    std::uint64_t shortReports_ = 0;
    std::uint64_t truncatedReports_ = 0;
    std::uint64_t droppedSamples_ = 0;
    std::size_t maximumObservedReportSize_ = 0;
    bool initialized_ = false;
};

struct Apex4GyroCaptureMetadata {
    std::string model;
    std::string connection;
    std::uint8_t connectionRaw = 0;
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    std::size_t declaredInputReportLength = 0;
    unsigned int phaseSeconds = 0;
    bool interrupted = false;
};

[[nodiscard]] bool isKnownApex4ControlOffset(std::size_t offset) noexcept;
[[nodiscard]] std::string formatApex4GyroCaptureJson(
    const Apex4GyroCaptureMetadata& metadata,
    const std::vector<Apex4MotionPhaseCapture>& phases);

} // namespace asb::diagnostics
