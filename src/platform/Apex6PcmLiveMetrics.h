#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace asb::platform {

#pragma pack(push, 1)
struct Apex6PcmLiveBlock {
    std::uint32_t magic = 0x50365041; // "AP6P"
    std::uint32_t version = 1;
    std::uint32_t measurementActive = 0;
    std::uint32_t measurementGeneration = 0;

    std::uint16_t currentLeftPeak16 = 0;
    std::uint16_t currentRightPeak16 = 0;
    std::uint16_t rawHapticLeftPeak16 = 0;
    std::uint16_t rawHapticRightPeak16 = 0;
    std::uint8_t  motorOutputLeftPeak8 = 0;
    std::uint8_t  motorOutputRightPeak8 = 0;
    std::uint8_t  audioMixChannels = 0;
    std::uint8_t  audioFormatValid = 0;

    std::uint32_t sessionGenerationEcho = 0;
    std::uint16_t measuredLeftPeak16 = 0;
    std::uint16_t measuredRightPeak16 = 0;
    std::uint16_t measuredMaxPeak16 = 0;
    std::uint16_t rawMeasuredHapticPeak16 = 0;
    std::uint32_t measuredActiveBlocks = 0;
    std::uint32_t measuredSilentBlocks = 0;
    std::uint32_t measuredDroppedBlocks = 0;
    std::uint32_t measuredClippedSamples = 0;
    std::uint32_t measuredProcessedSamples = 0;
    std::uint64_t measuredElapsedMs = 0;
};
#pragma pack(pop)

inline constexpr std::string_view kApex6PcmLiveGlobalMapping = "Local\\ApexSenseBridge.Apex6Pcm.Live";

class Apex6PcmLiveServer {
public:
    explicit Apex6PcmLiveServer(std::string_view sessionToken = {}) noexcept {
#ifdef _WIN32
        std::string name(kApex6PcmLiveGlobalMapping);
        if (!sessionToken.empty()) {
            name += ".";
            name += sessionToken;
        }
        mapping_ = CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
            static_cast<DWORD>(sizeof(Apex6PcmLiveBlock)), name.c_str());
        if (mapping_ != NULL) {
            view_ = static_cast<Apex6PcmLiveBlock*>(
                MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Apex6PcmLiveBlock)));
            if (view_ != nullptr) {
                // Initialize header if freshly created
                if (view_->magic != 0x50365041) {
                    *view_ = Apex6PcmLiveBlock{};
                }
            }
        }
#endif
    }

    ~Apex6PcmLiveServer() {
#ifdef _WIN32
        if (view_ != nullptr) {
            UnmapViewOfFile(view_);
            view_ = nullptr;
        }
        if (mapping_ != NULL) {
            CloseHandle(mapping_);
            mapping_ = NULL;
        }
#endif
    }

    void updateLive(std::uint16_t left16, std::uint16_t right16,
                    std::uint16_t rawLeft16, std::uint16_t rawRight16,
                    std::uint8_t outLeft8, std::uint8_t outRight8,
                    std::uint8_t channels, bool quadraphonic,
                    bool hasActiveDrops, unsigned clippedCount) noexcept {
#ifdef _WIN32
        if (view_ == nullptr) return;

        view_->currentLeftPeak16 = left16;
        view_->currentRightPeak16 = right16;
        view_->rawHapticLeftPeak16 = rawLeft16;
        view_->rawHapticRightPeak16 = rawRight16;
        view_->motorOutputLeftPeak8 = outLeft8;
        view_->motorOutputRightPeak8 = outRight8;
        view_->audioMixChannels = channels;
        view_->audioFormatValid = quadraphonic ? 1 : 0;

        // Check if measurement is active
        if (view_->measurementActive != 0) {
            // Check for new measurement generation/start
            if (view_->sessionGenerationEcho != view_->measurementGeneration) {
                view_->sessionGenerationEcho = view_->measurementGeneration;
                view_->measuredLeftPeak16 = 0;
                view_->measuredRightPeak16 = 0;
                view_->measuredMaxPeak16 = 0;
                view_->rawMeasuredHapticPeak16 = 0;
                view_->measuredActiveBlocks = 0;
                view_->measuredSilentBlocks = 0;
                view_->measuredDroppedBlocks = 0;
                view_->measuredClippedSamples = 0;
                view_->measuredProcessedSamples = 0;
                view_->measuredElapsedMs = 0;
                measurementStartedTicks_ = GetTickCount64();
            }

            if (left16 > view_->measuredLeftPeak16) view_->measuredLeftPeak16 = left16;
            if (right16 > view_->measuredRightPeak16) view_->measuredRightPeak16 = right16;
            const auto maxPeak = (std::max)(left16, right16);
            if (maxPeak > view_->measuredMaxPeak16) view_->measuredMaxPeak16 = maxPeak;

            const auto rawMax = (std::max)(rawLeft16, rawRight16);
            if (rawMax > view_->rawMeasuredHapticPeak16) view_->rawMeasuredHapticPeak16 = rawMax;

            if (left16 > 0 || right16 > 0) {
                view_->measuredActiveBlocks++;
            } else {
                view_->measuredSilentBlocks++;
            }

            if (hasActiveDrops) view_->measuredDroppedBlocks++;
            view_->measuredClippedSamples += clippedCount;
            view_->measuredProcessedSamples += 8; // 8 samples per channel block

            if (measurementStartedTicks_ > 0) {
                view_->measuredElapsedMs = GetTickCount64() - measurementStartedTicks_;
            }
        }
#else
        (void)left16; (void)right16; (void)rawLeft16; (void)rawRight16;
        (void)outLeft8; (void)outRight8; (void)channels; (void)quadraphonic;
        (void)hasActiveDrops; (void)clippedCount;
#endif
    }

    [[nodiscard]] bool isMeasurementActive() const noexcept {
#ifdef _WIN32
        return view_ != nullptr && view_->measurementActive != 0;
#else
        return false;
#endif
    }

private:
#ifdef _WIN32
    HANDLE mapping_ = NULL;
    Apex6PcmLiveBlock* view_ = nullptr;
    ULONGLONG measurementStartedTicks_ = 0;
#endif
};

} // namespace asb::platform
