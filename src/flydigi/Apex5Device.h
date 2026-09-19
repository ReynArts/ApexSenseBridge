#pragma once

#include "core/DeviceInfo.h"
#include "core/TriggerEffect.h"
#include "flydigi/Apex5Identity.h"
#include "platform/HidTransport.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace asb::flydigi {

struct TransportDeleter {
    void operator()(platform::HidTransport* transport) const noexcept;
};
using TransportPtr = std::unique_ptr<platform::HidTransport, TransportDeleter>;

class Apex5Device {
public:
    // Historical API name retained for source compatibility. This transport
    // now supports both Apex 5 (Flydigi V2) and Apex 4 (Flydigi V1/DInput).
    Apex5Device() = default;
    explicit Apex5Device(TransportPtr transport);

    Apex5Device(const Apex5Device&) = delete;
    Apex5Device& operator=(const Apex5Device&) = delete;

    // Joins the writer thread if one is running: it borrows this object's
    // transport, so it must not outlive it.
    ~Apex5Device();

    // Movable, but the writer thread does not travel: it captures this object's
    // address, so carrying it across a move would leave it writing through a
    // transport that had moved away. Both sides are stopped first. In practice
    // nothing moves an opened device - open() moves the freshly built one out,
    // before any writer exists.
    Apex5Device(Apex5Device&& other) noexcept;
    Apex5Device& operator=(Apex5Device&& other) noexcept;

    [[nodiscard]] static std::vector<HidDeviceInfo> findCandidates(std::string& error);
    [[nodiscard]] static std::optional<Apex5Device> open(const HidDeviceInfo& info, std::string& error);

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] const HidDeviceInfo& info() const;
    [[nodiscard]] const std::optional<Apex5Identity>& identity() const noexcept;

    bool verifyIdentity(std::string& error);

    bool setTrigger(const TriggerEffect& effect, std::string& error);
    bool setTriggerRaw(const ForceTriggerCommand& command, std::string& error);
    bool clearTrigger(TriggerSide side, std::string& error);
    bool clearAll(std::string& error);
    bool setRumble(std::uint8_t lowFrequencyMotor,
                   std::uint8_t highFrequencyMotor,
                   std::string& error);
    bool stopRumble(std::string& error);
    bool readProfileStatus(ProfileStatus& status, std::string& error);
    bool applyProfile(std::uint8_t slot, std::string& error);
    bool readInputTransportStatus(InputTransportStatus& status,
                                  std::string& error);
    bool setInputTransport(bool controllerData, bool rawData,
                           std::string& error);

    // The asynchronous write path, for the feedback bridges only.
    //
    // The 25 ms this pad needs between vendor commands used to be spent asleep
    // inside the feedback callback. That callback is the virtual device's
    // output path: uhid does not acknowledge a SET_REPORT until it returns and
    // libVIIPER holds its callback mutex throughout, so one report changing
    // both triggers and rumble could hold the game's write waiting for three
    // spacings. Worse, whatever had queued up behind it was applied afterwards,
    // by which time the game had usually asked for something else.
    //
    // These hand the newest value for a slot to a writer thread and return at
    // once. A slot written twice before the thread reaches it sends only the
    // newer value - coalescing, not a queue, because replaying a superseded
    // effect is the behaviour being removed, not preserved.
    bool startAsyncWrites(std::string& error);
    void stopAsyncWrites() noexcept;

    // With no writer thread running - every diagnostic command, and the tests -
    // these write synchronously and report failure by return value exactly as
    // before, so only the bridge path changes behaviour.
    bool queueTriggerRaw(const ForceTriggerCommand& command, std::string& error);
    bool queueRumble(std::uint8_t lowFrequencyMotor, std::uint8_t highFrequencyMotor,
                     std::string& error);

    // How many queued writes were retried. A handful over a session is the pad
    // dropping the odd command, which it does; a climbing number means the
    // output path is unwell even though the session survived.
    [[nodiscard]] std::uint64_t asyncWriteRetries() const noexcept;

    // A queued write that failed and kept failing. Latched, and cleared by
    // reading, so a caller polling this cannot miss one. A single failure does
    // not set it: the value goes back in its slot and the next cycle sends it
    // again, because one dropped command is not worth ending a session over.
    bool takeAsyncWriteError(std::string& error);

private:
    // Effect and rumble writes go through here rather than straight to the
    // transport. Two vendor commands sent close together lose the second one:
    // measured on an Apex 4, a release followed 4.3 ms later by an effect leaves
    // no resistance at all, while the same pair 10 ms apart works every time.
    //
    // That is exactly the shape a game produces. Cyberpunk 2077 sends a release
    // and then the effect 4.3 ms later on every aim after the first, and applies
    // both triggers from one report - so the left trigger, written second, was
    // the one that always vanished.
    [[nodiscard]] bool writeSpacedOutputReport(std::span<const std::uint8_t> report,
                                               std::string& error);

    [[nodiscard]] bool mayWriteEffects(std::string& error) const;
    [[nodiscard]] bool mayControlProfiles(std::string& error) const;
    [[nodiscard]] bool usesApex4Protocol() const noexcept;

    void writerLoop();

    TransportPtr transport_{};
    std::optional<Apex5Identity> identity_{};
    std::chrono::steady_clock::time_point lastVendorWriteAt_{};

    // Newest value per slot. The three share one spacing budget because the pad
    // does: the budget belongs to the device, not to either trigger.
    std::thread writer_{};
    std::mutex queueMutex_{};
    std::condition_variable queueSignal_{};
    std::optional<ForceTriggerCommand> pendingLeftTrigger_{};
    std::optional<ForceTriggerCommand> pendingRightTrigger_{};
    std::optional<std::pair<std::uint8_t, std::uint8_t>> pendingRumble_{};
    unsigned nextSlot_ = 0;
    bool writerStopping_ = false;

    std::atomic<bool> asyncWriteFailed_{false};
    std::atomic<std::uint64_t> asyncWriteRetries_{0};
    std::mutex asyncErrorMutex_{};
    std::string asyncError_{};
};

} // namespace asb::flydigi
