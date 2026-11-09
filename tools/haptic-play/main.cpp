#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <mmsystem.h>
#include <timeapi.h>
#include <tlhelp32.h>

#include "flydigi/Apex5Device.h"
#include "flydigi/Apex6Protocol.h"
#include "platform/HidTransport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace apex6 = asb::flydigi::apex6;
using Clock = std::chrono::steady_clock;

constexpr std::size_t kSamplesPerReport = 8;
constexpr auto kReportPeriod = std::chrono::milliseconds(8);
constexpr int kLimit = 112;
constexpr double kPi = 3.14159265358979323846;

std::atomic<bool> gStop{false};

BOOL WINAPI onConsoleEvent(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        gStop.store(true);
        return TRUE;
    }
    return FALSE;
}

bool processRunning(const wchar_t* name) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok && !found; ok = Process32NextW(snapshot, &entry)) {
        found = _wcsicmp(entry.szExeFile, name) == 0;
    }
    CloseHandle(snapshot);
    return found;
}

class HardwareLock {
public:
    bool acquire(std::string& error) {
        wchar_t buffer[MAX_PATH] = {};
        ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\Temp\\asb-apex6-hw.lock", buffer, MAX_PATH);
        path_ = buffer;
        if (!CreateDirectoryW(path_.c_str(), nullptr)) {
            error = "the APEX 6 hardware lock already exists: another session is using the pad";
            return false;
        }
        held_ = true;
        return true;
    }

    ~HardwareLock() {
        if (held_) RemoveDirectoryW(path_.c_str());
    }

private:
    std::wstring path_;
    bool held_ = false;
};

class DeadlineTimer {
public:
    DeadlineTimer() {
        constexpr DWORD kHighResolution = 0x00000002;
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, kHighResolution, TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!timer_) timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
    }

    ~DeadlineTimer() {
        if (timer_) CloseHandle(timer_);
    }

    void waitUntil(Clock::time_point deadline) const {
        const auto remaining = deadline - Clock::now();
        if (remaining <= Clock::duration::zero()) return;
        if (!timer_) {
            std::this_thread::sleep_for(remaining);
            return;
        }
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>((nanoseconds + 99) / 100);
        if (due.QuadPart == 0) due.QuadPart = -1;
        if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer_, INFINITE);
    }

private:
    HANDLE timer_ = nullptr;
};

struct Variant {
    std::string label;
    std::string path;
    std::vector<std::int8_t> data;
};

struct Options {
    std::vector<std::pair<std::string, std::string>> inputs;
    std::size_t startMs = 0;
    std::size_t lengthMs = 12000;
    unsigned scalePercent = 100;
    unsigned gapMs = 1500;
    bool blind = false;
    bool force = false;
    std::string keyPath;
    std::string audioPath;
    bool preview = false;
    unsigned audioLeadMs = 0;
};

class Player {
public:
    Player(asb::flydigi::Apex5Device& device, unsigned scalePercent)
        : device_(device), scale_(static_cast<double>(scalePercent) / 100.0), next_(Clock::now()) {}

    bool failed() const { return failed_; }
    const std::string& error() const { return error_; }

    void silence(std::size_t milliseconds) {
        const auto reports = (milliseconds + 7) / 8;
        for (std::size_t index = 0; index < reports && !gStop.load() && !failed(); ++index) writeBlock(nullptr, 0);
    }

    void play(const Variant& variant, std::size_t startMs, std::size_t lengthMs) {
        const std::size_t frames = variant.data.size() / 2;
        const std::size_t first = std::min(startMs, frames);
        const std::size_t last = std::min(first + lengthMs, frames);
        for (std::size_t position = first; position < last && !gStop.load() && !failed();
             position += kSamplesPerReport) {
            writeBlock(variant.data.data() + position * 2, std::min(kSamplesPerReport, last - position));
        }
    }

    void pulse(std::size_t milliseconds, int amplitude) {
        const std::size_t reports = (milliseconds + 7) / 8;
        for (std::size_t report = 0; report < reports && !gStop.load() && !failed(); ++report) {
            apex6::MotorBlock block{};
            for (std::size_t index = 0; index < kSamplesPerReport; ++index) {
                const double phase = 0.25 + 0.065 * static_cast<double>(report * kSamplesPerReport + index);
                const auto value = static_cast<std::int8_t>(std::lround(std::sin(2.0 * kPi * phase) * amplitude));
                block[index].leftGrip = value;
                block[index].rightGrip = value;
            }
            send(block);
        }
    }

    void cue(unsigned count) {
        for (unsigned index = 0; index < count; ++index) {
            pulse(70, 100);
            silence(220);
        }
    }
private:
    std::int8_t scaled(std::int8_t value) const {
        const double result = std::round(static_cast<double>(value) * scale_);
        return static_cast<std::int8_t>(std::clamp(result, -static_cast<double>(kLimit), static_cast<double>(kLimit)));
    }

    void writeBlock(const std::int8_t* interleaved, std::size_t frames) {
        apex6::MotorBlock block{};
        for (std::size_t index = 0; index < frames; ++index) {
            block[index].leftGrip = scaled(interleaved[index * 2]);
            block[index].rightGrip = scaled(interleaved[index * 2 + 1]);
        }
        send(block);
    }

    void send(const apex6::MotorBlock& block) {
        if (!device_.writeApex6Haptics(block, apex6::TriggerRoute::Mute, false, true, error_)) {
            failed_ = true;
            if (error_.empty()) error_ = "write failed";
            return;
        }
        next_ += kReportPeriod;
        timer_.waitUntil(next_);
        if (Clock::now() > next_ + kReportPeriod) next_ = Clock::now();
    }

    asb::flydigi::Apex5Device& device_;
    double scale_;
    Clock::time_point next_;
    DeadlineTimer timer_;
    std::string error_;
    bool failed_ = false;
};

bool loadStream(const std::string& path, std::vector<std::int8_t>& data) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    data.assign(reinterpret_cast<const std::int8_t*>(bytes.data()),
                reinterpret_cast<const std::int8_t*>(bytes.data()) + bytes.size());
    return data.size() >= 2;
}

void beepCue(unsigned count) {
    std::thread([count] {
        for (unsigned index = 0; index < count; ++index) {
            Beep(880, 110);
            Sleep(140);
        }
    }).detach();
}

void usage() {
    std::puts("usage: ApexSenseBridgeHapticPlay <label=stream.i8> <label=stream.i8> [...]\n"
              "         [--start <ms>] [--length <ms>] [--scale <percent>] [--gap <ms>] [--blind <key-file>] [--force]\n"
              "         [--audio <pcm16.wav>] [--audio-lead <ms>] [--preview]\n"
              "Plays interleaved L/R int8 streams (1 kHz, from the bench --dump) on the grips of an APEX 6 Pro.\n"
              "Each variant is announced by N beeps and by N short buzzes in the grips, the end by one long buzz.\n"
              "Variant N is announced by N beeps. With --blind the order is shuffled and the key written to the file.");
}

bool parse(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const bool hasValue = index + 1 < argc;
        if (argument == "--start" && hasValue) options.startMs = static_cast<std::size_t>(std::atoll(argv[++index]));
        else if (argument == "--length" && hasValue) options.lengthMs = static_cast<std::size_t>(std::atoll(argv[++index]));
        else if (argument == "--scale" && hasValue) options.scalePercent = static_cast<unsigned>(std::atoi(argv[++index]));
        else if (argument == "--gap" && hasValue) options.gapMs = static_cast<unsigned>(std::atoi(argv[++index]));
        else if (argument == "--blind" && hasValue) {
            options.blind = true;
            options.keyPath = argv[++index];
        } else if (argument == "--audio" && hasValue) options.audioPath = argv[++index];
        else if (argument == "--audio-lead" && hasValue) options.audioLeadMs = static_cast<unsigned>(std::atoi(argv[++index]));
        else if (argument == "--preview") options.preview = true;
        else if (argument == "--force") options.force = true;
        else if (argument.find('=') != std::string::npos && argument.rfind("--", 0) != 0) {
            const auto split = argument.find('=');
            options.inputs.emplace_back(argument.substr(0, split), argument.substr(split + 1));
        } else return false;
    }
    return options.inputs.size() >= 2 && options.scalePercent >= 1 && options.scalePercent <= 400;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse(argc, argv, options)) {
        usage();
        return 2;
    }
    std::vector<Variant> variants;
    for (const auto& [label, path] : options.inputs) {
        Variant variant;
        variant.label = label;
        variant.path = path;
        if (!loadStream(path, variant.data)) {
            std::fprintf(stderr, "cannot read %s\n", path.c_str());
            return 2;
        }
        variants.push_back(std::move(variant));
    }
    std::vector<std::size_t> order(variants.size());
    for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
    if (options.blind) {
        std::mt19937 generator(static_cast<std::uint32_t>(Clock::now().time_since_epoch().count()));
        std::shuffle(order.begin(), order.end(), generator);
        std::ofstream key(options.keyPath);
        for (std::size_t position = 0; position < order.size(); ++position) {
            key << (position + 1) << ' ' << variants[order[position]].label << '\n';
        }
    }

    SetConsoleCtrlHandler(onConsoleEvent, TRUE);
    timeBeginPeriod(1);
    const wchar_t* conflicts[] = {L"ApexSenseBridge.exe", L"ApexSenseBridgeTray.exe", L"xHaptics Engine.exe",
                                  L"XHaptics.exe", L"x-haptic-engine-fusion.exe"};
    for (const auto* name : conflicts) {
        if (processRunning(name) && !options.force) {
            std::fprintf(stderr, "refusing to run: %ls is running and would also write to the pad\n", name);
            timeEndPeriod(1);
            return 3;
        }
    }
    HardwareLock lock;
    std::string error;
    if (!lock.acquire(error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        timeEndPeriod(1);
        return 3;
    }
    const auto candidates = asb::flydigi::Apex5Device::findCandidates(error);
    const asb::HidDeviceInfo* chosen = nullptr;
    for (const auto& candidate : candidates) {
        if (apex6::isProduct(candidate.vendorId, candidate.productId) && candidate.usagePage == apex6::kUsagePage) {
            chosen = &candidate;
            break;
        }
    }
    if (!chosen) {
        std::fprintf(stderr, "no APEX 6 Pro vendor interface found%s%s\n", error.empty() ? "" : ": ", error.c_str());
        timeEndPeriod(1);
        return 3;
    }
    asb::flydigi::TransportPtr transport(asb::platform::createHidTransport(*chosen, error));
    if (!transport || !transport->isOpen()) {
        std::fprintf(stderr, "cannot open the APEX 6 Pro: %s\n", error.c_str());
        timeEndPeriod(1);
        return 3;
    }
    asb::flydigi::Apex5Device device(std::move(transport));
    if (!device.verifyIdentity(error) || !device.identity() || !device.identity()->supportsRealtimeHaptics()) {
        std::fprintf(stderr, "identity check failed or realtime haptics unsupported: %s\n", error.c_str());
        timeEndPeriod(1);
        return 3;
    }
    std::printf("device: %s\n", device.identity()->describe().c_str());
    if (!device.enableApex6Haptics(error)) {
        std::fprintf(stderr, "motor enable failed: %s\n", error.c_str());
        timeEndPeriod(1);
        return 3;
    }

    Player player(device, options.scalePercent);
    player.silence(300);
    std::printf("%zu variants, %zu ms each, scale %u%%, %s order\n", variants.size(), options.lengthMs,
                options.scalePercent, options.blind ? "blind" : "listed");
    if (options.preview && !options.audioPath.empty()) {
        std::printf("preview: the sound alone, no vibration\n");
        player.pulse(400, 100);
        player.silence(600);
        PlaySoundA(options.audioPath.c_str(), nullptr, SND_FILENAME | SND_ASYNC);
        player.silence(options.lengthMs + 800);
        PlaySoundA(nullptr, nullptr, 0);
        player.silence(2500);
    }
    for (std::size_t position = 0; position < order.size() && !gStop.load() && !player.failed(); ++position) {
        const auto& variant = variants[order[position]];
        if (options.blind) std::printf("variant %zu of %zu: %zu beep(s)\n", position + 1, order.size(), position + 1);
        else std::printf("variant %zu of %zu: %s\n", position + 1, order.size(), variant.label.c_str());
        beepCue(static_cast<unsigned>(position + 1));
        player.cue(static_cast<unsigned>(position + 1));
        player.silence(700);
        if (!options.audioPath.empty()) {
            PlaySoundA(options.audioPath.c_str(), nullptr, SND_FILENAME | SND_ASYNC);
            player.silence(options.audioLeadMs);
        }
        player.play(variant, options.startMs, options.lengthMs);
        if (!options.audioPath.empty()) PlaySoundA(nullptr, nullptr, 0);
        player.silence(options.gapMs);
    }
    beepCue(1);
    player.pulse(500, 100);
    player.silence(400);
    if (player.failed()) std::fprintf(stderr, "playback error: %s\n", player.error().c_str());
    std::string disableError;
    if (!device.disableApex6Haptics(disableError)) {
        std::fprintf(stderr, "motor disable failed: %s\n", disableError.c_str());
    }
    timeEndPeriod(1);
    return player.failed() ? 1 : 0;
}
