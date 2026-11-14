#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/SessionControl.h"
#include "platform/SessionTuning.h"

#include <atomic>
#include <memory>
#include <new>
#include <string>

namespace asb::platform {
namespace {

std::uint32_t loadField(std::uint32_t& field) noexcept {
    return std::atomic_ref<std::uint32_t>(field).load(std::memory_order_relaxed);
}

void storeField(std::uint32_t& field, std::uint32_t value) noexcept {
    std::atomic_ref<std::uint32_t>(field).store(value, std::memory_order_relaxed);
}

class WindowsSessionTuning final : public SessionTuning {
public:
    WindowsSessionTuning(HANDLE mapping, SessionTuningBlock* block) noexcept
        : mapping_(mapping), block_(block) {}

    ~WindowsSessionTuning() override {
        if (block_) UnmapViewOfFile(block_);
        if (mapping_) CloseHandle(mapping_);
    }

    [[nodiscard]] bool poll(SessionTuningRequest& request) noexcept override {
        std::atomic_ref<std::uint32_t> sequence(block_->request.sequence);
        const auto before = sequence.load(std::memory_order_acquire);
        if (before == lastSequence_ || (before & 1U) != 0U) return false;

        SessionTuningRequest copy{};
        auto& source = block_->request;
        copy.triggerStrengthPercent = loadField(source.triggerStrengthPercent);
        copy.vibrationStrengthPercent = loadField(source.vibrationStrengthPercent);
        copy.gripMute = loadField(source.gripMute);
        copy.triggerEffectsOff = loadField(source.triggerEffectsOff);
        copy.touchpadProfile = loadField(source.touchpadProfile);
        copy.apexProfileSlot = loadField(source.apexProfileSlot);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (sequence.load(std::memory_order_relaxed) != before) return false;

        copy.sequence = before;
        lastSequence_ = before;
        request = copy;
        return true;
    }

    void publishApplied(const SessionTuningApplied& applied) noexcept override {
        auto& target = block_->applied;
        std::atomic_ref<std::uint32_t> sequence(target.sequence);
        const auto odd = (sequence.load(std::memory_order_relaxed) | 1U);
        sequence.store(odd, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        storeField(target.requestSequence, applied.requestSequence);
        storeField(target.triggerStrengthPercent, applied.triggerStrengthPercent);
        storeField(target.vibrationStrengthPercent, applied.vibrationStrengthPercent);
        storeField(target.gripMute, applied.gripMute);
        storeField(target.triggerEffectsOff, applied.triggerEffectsOff);
        storeField(target.touchpadProfile, applied.touchpadProfile);
        storeField(target.apexProfileSlot, applied.apexProfileSlot);
        storeField(target.sampleRateHz, applied.sampleRateHz);
        storeField(target.controllerModel, applied.controllerModel);
        sequence.store(odd + 1U, std::memory_order_release);
    }

private:
    HANDLE mapping_ = nullptr;
    SessionTuningBlock* block_ = nullptr;
    std::uint32_t lastSequence_ = 0;
};

} // namespace

std::unique_ptr<SessionTuning> createSessionTuning(std::string_view token, std::string& error) {
    if (!isValidSessionToken(token)) {
        error = "Session tuning requires a valid session token.";
        return nullptr;
    }
    const auto narrow = sessionTuningMappingName(token);
    const std::wstring name(narrow.begin(), narrow.end());
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        static_cast<DWORD>(kSessionTuningBlockSize),
                                        name.c_str());
    if (!mapping) {
        error = "CreateFileMappingW(session tuning) failed with Windows error " +
                std::to_string(GetLastError()) + '.';
        return nullptr;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mapping);
        error = "The session tuning mapping already exists.";
        return nullptr;
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                               kSessionTuningBlockSize);
    if (!view) {
        error = "MapViewOfFile(session tuning) failed with Windows error " +
                std::to_string(GetLastError()) + '.';
        CloseHandle(mapping);
        return nullptr;
    }
    auto* block = new (view) SessionTuningBlock{};
    return std::make_unique<WindowsSessionTuning>(mapping, block);
}

} // namespace asb::platform
