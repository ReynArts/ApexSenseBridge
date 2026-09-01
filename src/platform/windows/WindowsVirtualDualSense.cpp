#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "dualsense/VirtualDualSense.h"
#include "dualsense/ViiperProtocol.h"
#include "platform/windows/WindowsVirtualDualSenseBackends.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace asb::dualsense {
namespace {

constexpr std::uintptr_t kInvalidSocket = ~static_cast<std::uintptr_t>(0);

SOCKET toSocket(std::uintptr_t value) {
    return static_cast<SOCKET>(value);
}

std::uintptr_t fromSocket(SOCKET value) {
    return static_cast<std::uintptr_t>(value);
}

bool ensureWinsock() {
    struct Runtime {
        Runtime() {
            WSADATA data{};
            ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }
        ~Runtime() {
            if (ready) {
                WSACleanup();
            }
        }
        bool ready = false;
    };
    static Runtime runtime;
    return runtime.ready;
}

void closeSocket(SOCKET socket) {
    if (socket == INVALID_SOCKET) {
        return;
    }
    shutdown(socket, SD_BOTH);
    closesocket(socket);
}

bool sendAll(SOCKET socket, const std::uint8_t* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const auto chunk = static_cast<int>(std::min<std::size_t>(size - sent, 16 * 1024));
        const int result = send(socket,
                                reinterpret_cast<const char*>(data + sent),
                                chunk,
                                0);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

bool sendAll(SOCKET socket, const std::string& data) {
    return sendAll(socket,
                   reinterpret_cast<const std::uint8_t*>(data.data()),
                   data.size());
}

bool receiveExact(SOCKET socket, std::uint8_t* data, std::size_t size) {
    std::size_t received = 0;
    while (received < size) {
        const auto chunk = static_cast<int>(std::min<std::size_t>(size - received, 4096));
        const int result = recv(socket, reinterpret_cast<char*>(data + received), chunk, 0);
        if (result <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(result);
    }
    return true;
}

// The VIIPER stream can contain several small feedback frames in one TCP
// segment. Keeping unread bytes here avoids two recv syscalls for every frame.
class BufferedSocketReader {
public:
    explicit BufferedSocketReader(SOCKET socket) noexcept : socket_(socket) {}

    bool readExact(std::uint8_t* destination, std::size_t size) noexcept {
        std::size_t copied = 0;
        while (copied < size) {
            if (begin_ == end_) {
                const int received = recv(
                    socket_, reinterpret_cast<char*>(buffer_.data()),
                    static_cast<int>(buffer_.size()), 0);
                if (received <= 0) return false;
                begin_ = 0;
                end_ = static_cast<std::size_t>(received);
            }
            const auto available = end_ - begin_;
            const auto chunk = (std::min)(available, size - copied);
            std::copy_n(buffer_.data() + begin_, chunk, destination + copied);
            begin_ += chunk;
            copied += chunk;
        }
        return true;
    }

    [[nodiscard]] bool hasBufferedData() const noexcept {
        return begin_ != end_;
    }

private:
    SOCKET socket_ = INVALID_SOCKET;
    std::array<std::uint8_t, 4096> buffer_{};
    std::size_t begin_ = 0;
    std::size_t end_ = 0;
};

int waitForReadable(SOCKET socket, std::chrono::milliseconds timeout) noexcept {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(socket, &readSet);
    timeval value{};
    value.tv_sec = static_cast<long>(timeout.count() / 1000);
    value.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
    return select(0, &readSet, nullptr, nullptr, &value);
}

bool receiveUntilClose(SOCKET socket, std::string& response) {
    response.clear();
    std::array<char, 4096> buffer{};
    constexpr std::size_t maximumResponseSize = 1024 * 1024;
    for (;;) {
        const int result = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (result > 0) {
            if (response.size() + static_cast<std::size_t>(result) > maximumResponseSize) {
                return false;
            }
            response.append(buffer.data(), static_cast<std::size_t>(result));
            continue;
        }
        if (result == 0) {
            break;
        }
        return false;
    }
    while (!response.empty() && (response.back() == '\r' || response.back() == '\n')) {
        response.pop_back();
    }
    return !response.empty();
}

SOCKET connectLoopback(std::uint16_t port,
                       int timeoutMilliseconds,
                       int connectTimeoutMilliseconds = 100) {
    if (!ensureWinsock()) {
        return INVALID_SOCKET;
    }

    const SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        closesocket(socket);
        return INVALID_SOCKET;
    }

    // SO_RCVTIMEO/SO_SNDTIMEO do not bound a blocking connect(). On machines
    // whose local firewall delays a refused loopback connection, the initial
    // "is VIIPER already running?" probe used to stall Playnite for about two
    // seconds. Complete the connect non-blockingly and cap only this phase.
    u_long nonBlocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        closesocket(socket);
        return INVALID_SOCKET;
    }
    const int connectResult =
        connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (connectResult == SOCKET_ERROR) {
        const int connectError = WSAGetLastError();
        if (connectError != WSAEWOULDBLOCK && connectError != WSAEINPROGRESS &&
            connectError != WSAEALREADY) {
            closesocket(socket);
            return INVALID_SOCKET;
        }

        fd_set writable;
        fd_set exceptional;
        FD_ZERO(&writable);
        FD_ZERO(&exceptional);
        FD_SET(socket, &writable);
        FD_SET(socket, &exceptional);
        timeval connectTimeout{};
        connectTimeout.tv_sec = connectTimeoutMilliseconds / 1000;
        connectTimeout.tv_usec = (connectTimeoutMilliseconds % 1000) * 1000;
        const int selected = select(
            0, nullptr, &writable, &exceptional, &connectTimeout);
        if (selected <= 0) {
            closesocket(socket);
            return INVALID_SOCKET;
        }

        int socketError = 0;
        int socketErrorLength = sizeof(socketError);
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char*>(&socketError),
                       &socketErrorLength) == SOCKET_ERROR || socketError != 0) {
            closesocket(socket);
            return INVALID_SOCKET;
        }
    }

    nonBlocking = 0;
    if (ioctlsocket(socket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        closesocket(socket);
        return INVALID_SOCKET;
    }

    const int noDelay = 1;
    (void)setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    (void)setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeoutMilliseconds),
                     sizeof(timeoutMilliseconds));
    (void)setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                     reinterpret_cast<const char*>(&timeoutMilliseconds),
                     sizeof(timeoutMilliseconds));
    return socket;
}

std::filesystem::path executableDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr,
                                            buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

std::filesystem::path writableLogPath() {
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA",
                                            buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    std::filesystem::path base;
    if (length > 0 && length < buffer.size()) {
        base = std::wstring(buffer.data(), length);
    } else {
        length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (length > 0 && length < buffer.size()) {
            base = std::wstring(buffer.data(), length);
        }
    }
    if (base.empty()) {
        return {};
    }

    const auto directory = base / L"ApexSenseBridge";
    if (!CreateDirectoryW(directory.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return {};
    }
    const auto logPath = directory / L"viiper.log";
    // Logging is optional. A stale file can have an ACL inherited from a
    // different account (service, sandbox, previous install); do not let that
    // prevent the virtual controller itself from starting.
    const HANDLE probe = CreateFileW(
        logPath.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (probe == INVALID_HANDLE_VALUE) {
        return {};
    }
    CloseHandle(probe);
    return logPath;
}

bool regularFileExists(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring quoteArgument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(character);
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

enum class PingStatus {
    Ready,
    Unavailable,
    Incompatible,
};

class ViiperVirtualDualSense final : public VirtualDualSense {
public:
    explicit ViiperVirtualDualSense(VirtualDualSenseOptions options)
        : options_(std::move(options)) {}

    ~ViiperVirtualDualSense() override {
        close();
    }

    bool open(std::string& error, FeedbackHandler handler) override {
        close();
        resetStats();
        handler_ = std::move(handler);
        const auto elapsedMicroseconds = [](const auto startedAt) {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - startedAt).count());
        };

        const auto bootstrapStartedAt = std::chrono::steady_clock::now();
        if (!ensureServer(error)) {
            handler_ = {};
            return false;
        }
        initializationBootstrapUs_ = elapsedMicroseconds(bootstrapStartedAt);

        const auto busStartedAt = std::chrono::steady_clock::now();
        if (!createBus(error)) {
            close();
            return false;
        }
        initializationBusUs_ = elapsedMicroseconds(busStartedAt);

        const auto deviceStartedAt = std::chrono::steady_clock::now();
        if (!addDevice(error)) {
            close();
            return false;
        }
        initializationDeviceUs_ = elapsedMicroseconds(deviceStartedAt);

        const auto feedbackStartedAt = std::chrono::steady_clock::now();
        if (!openStream(error)) {
            close();
            return false;
        }
        initializationFeedbackUs_ = elapsedMicroseconds(feedbackStartedAt);
        return true;
    }

    void close() noexcept override {
        running_.store(false, std::memory_order_relaxed);
        connected_.store(false, std::memory_order_relaxed);

        const auto socketValue = streamSocket_.exchange(kInvalidSocket, std::memory_order_acq_rel);
        if (socketValue != kInvalidSocket) {
            closeSocket(toSocket(socketValue));
        }
        if (feedbackThread_.joinable()) {
            feedbackThread_.join();
        }

        try {
            removeDeviceAndBus();
        } catch (...) {
            busId_ = 0;
            deviceId_.clear();
        }
        stopSpawnedServer();
        handler_ = {};
    }

    VirtualDualSenseStats stats() const override {
        VirtualDualSenseStats result{};
        result.connected = connected_.load(std::memory_order_relaxed);
        result.inputUpdates = inputUpdates_.load(std::memory_order_relaxed);
        result.outputReports = outputReports_.load(std::memory_order_relaxed);
        result.triggerReports = triggerReports_.load(std::memory_order_relaxed);
        result.rumbleReports = rumbleReports_.load(std::memory_order_relaxed);
        result.audioHapticsFrames = audioHapticsFrames_.load(std::memory_order_relaxed);
        result.audioHapticsDelivered =
            audioHapticsDelivered_.load(std::memory_order_relaxed);
        result.audioHapticsCoalesced =
            audioHapticsCoalesced_.load(std::memory_order_relaxed);
        result.malformedFrames = malformedFrames_.load(std::memory_order_relaxed);
        result.unknownFrames = unknownFrames_.load(std::memory_order_relaxed);
        result.initializationBootstrapUs = initializationBootstrapUs_;
        result.initializationServerUs = initializationServerUs_;
        result.initializationBusUs = initializationBusUs_;
        result.initializationDeviceUs = initializationDeviceUs_;
        result.initializationFeedbackUs = initializationFeedbackUs_;
        result.initializationInputUs = initializationInputUs_;
        result.backendVersion = backendVersion_;
        return result;
    }

    bool connected() const noexcept override {
        return connected_.load(std::memory_order_relaxed);
    }

    bool updateInput(const DualSenseInputState& state, std::string& error) override {
        const auto socketValue = streamSocket_.load(std::memory_order_acquire);
        if (socketValue == kInvalidSocket || !connected_.load(std::memory_order_relaxed)) {
            error = "Virtual DualSense input stream is not connected.";
            return false;
        }
        const auto input = buildViiperInput(state);
        std::lock_guard lock(inputWriteMutex_);
        if (!sendAll(toSocket(socketValue), input.data(), input.size())) {
            error = "Could not update the virtual DualSense input state.";
            return false;
        }
        inputUpdates_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

private:
    void resetStats() {
        connected_.store(false, std::memory_order_relaxed);
        inputUpdates_.store(0, std::memory_order_relaxed);
        outputReports_.store(0, std::memory_order_relaxed);
        triggerReports_.store(0, std::memory_order_relaxed);
        rumbleReports_.store(0, std::memory_order_relaxed);
        audioHapticsFrames_.store(0, std::memory_order_relaxed);
        audioHapticsDelivered_.store(0, std::memory_order_relaxed);
        audioHapticsCoalesced_.store(0, std::memory_order_relaxed);
        malformedFrames_.store(0, std::memory_order_relaxed);
        unknownFrames_.store(0, std::memory_order_relaxed);
        initializationBootstrapUs_ = 0;
        initializationServerUs_ = 0;
        initializationBusUs_ = 0;
        initializationDeviceUs_ = 0;
        initializationFeedbackUs_ = 0;
        initializationInputUs_ = 0;
        backendVersion_.clear();
    }

    bool request(const std::string& path,
                 const std::string& payload,
                 std::string& response) const {
        const SOCKET socket = connectLoopback(options_.apiPort, 2000);
        if (socket == INVALID_SOCKET) {
            return false;
        }
        const std::string message = viiper::buildRequest(path, payload);
        const bool success = sendAll(socket, message) && receiveUntilClose(socket, response);
        closesocket(socket);
        return success;
    }

    PingStatus ping(std::string& version) const {
        std::string response;
        if (!request("ping", {}, response)) {
            return PingStatus::Unavailable;
        }

        std::string server;
        if (!viiper::parsePingResponse(response, server, version) || server != "VIIPER" ||
            !viiper::isDualSenseCompatibleVersion(version)) {
            return PingStatus::Incompatible;
        }
        return PingStatus::Ready;
    }

    bool ensureServer(std::string& error) {
        std::string version;
        const PingStatus initialStatus = ping(version);
        if (initialStatus == PingStatus::Ready) {
            backendVersion_ = std::move(version);
            return true;
        }
        if (initialStatus == PingStatus::Incompatible) {
            error = "A server is listening on the configured local VIIPER API port but "
                    "it does not advertise the adaptive-trigger and audio-haptics "
                    "extensions required by ApexSenseBridge.";
            return false;
        }

        std::filesystem::path viiperPath = options_.viiperExecutable;
        if (viiperPath.empty()) {
            viiperPath = executableDirectory() / L"viiper.exe";
        }
        if (!regularFileExists(viiperPath)) {
            error = "Patched viiper.exe was not found at: " + viiperPath.string() +
                    ". Place v0.7.0-asb3 beside ApexSenseBridge.exe or pass --viiper PATH.";
            return false;
        }
        if (!spawnServer(viiperPath, error)) {
            return false;
        }

        // VIIPER normally binds loopback in well under 250 ms. Probe at 25 ms
        // granularity so the old fixed quarter-second sleep is not paid on
        // every Playnite launch, while preserving the same five-second cap.
        for (int attempt = 0; attempt < 200; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            const PingStatus status = ping(version);
            if (status == PingStatus::Ready) {
                backendVersion_ = std::move(version);
                return true;
            }
            if (status == PingStatus::Incompatible) {
                error = "The started VIIPER sidecar is incompatible; a patched DualSense backend is required.";
                stopSpawnedServer();
                return false;
            }
            if (serverProcess_ && WaitForSingleObject(serverProcess_, 0) == WAIT_OBJECT_0) {
                break;
            }
        }

        error = "Patched viiper.exe did not start its local API server.";
        stopSpawnedServer();
        return false;
    }

    bool spawnServer(const std::filesystem::path& path, std::string& error) {
        const std::wstring executable = path.wstring();
        std::wstring commandLine = quoteArgument(executable) + L" server --update-notify none";
        if (options_.apiPort != 3242) {
            commandLine += L" --api.addr=127.0.0.1:" +
                           std::to_wstring(options_.apiPort);
        }
        logPath_ = writableLogPath();
        if (!logPath_.empty()) {
            commandLine += L" " + quoteArgument(L"--log.file=" + logPath_.wstring());
        }
        std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
        mutableCommand.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        const std::wstring workingDirectory = path.parent_path().wstring();

        if (!CreateProcessW(executable.c_str(),
                            mutableCommand.data(),
                            nullptr,
                            nullptr,
                            FALSE,
                            CREATE_NO_WINDOW,
                            nullptr,
                            workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                            &startup,
                            &process)) {
            std::ostringstream message;
            message << "Failed to start patched viiper.exe (Win32 error " << GetLastError() << ").";
            error = message.str();
            return false;
        }

        CloseHandle(process.hThread);
        serverProcess_ = process.hProcess;
        spawnedServer_ = true;
        return true;
    }

    void stopSpawnedServer() noexcept {
        if (!spawnedServer_ || !serverProcess_) {
            return;
        }
        if (WaitForSingleObject(serverProcess_, 0) == WAIT_TIMEOUT) {
            (void)TerminateProcess(serverProcess_, 0);
            (void)WaitForSingleObject(serverProcess_, 2000);
        }
        CloseHandle(serverProcess_);
        serverProcess_ = nullptr;
        spawnedServer_ = false;
    }

    bool createBus(std::string& error) {
        std::string response;
        std::uint32_t busId = 0;
        if (!request("bus/create", "0", response) ||
            !viiper::parseBusResponse(response, busId) || busId == 0) {
            error = "VIIPER failed to create a virtual USB bus.";
            return false;
        }
        busId_ = busId;
        return true;
    }

    bool addDevice(std::string& error) {
        std::ostringstream path;
        path << "bus/" << busId_ << "/add";
        std::string response;
        if (!request(path.str(), "{\"type\":\"dualsense\"}", response)) {
            error = "VIIPER failed to create the virtual DualSense.";
            return false;
        }
        if (viiper::isUsbIpDriverMissingResponse(response)) {
            error = "The usbip-win2 UDE driver required by VIIPER is not installed. No driver was installed automatically.";
            return false;
        }

        std::uint32_t returnedBusId = 0;
        std::string deviceId;
        if (!viiper::parseDeviceResponse(response, returnedBusId, deviceId) ||
            returnedBusId != busId_ || deviceId.empty()) {
            error = "VIIPER returned an invalid virtual DualSense creation response.";
            return false;
        }
        deviceId_ = std::move(deviceId);
        return true;
    }

    bool openStream(std::string& error) {
        const SOCKET socket = connectLoopback(options_.apiPort, 0);
        if (socket == INVALID_SOCKET) {
            error = "Could not connect to the VIIPER virtual DualSense stream.";
            return false;
        }

        const std::string streamPath = viiper::buildStreamPath(busId_, deviceId_);
        const auto neutralInput = buildNeutralViiperInput();
        if (!sendAll(socket, streamPath) ||
            !sendAll(socket, neutralInput.data(), neutralInput.size())) {
            closesocket(socket);
            error = "Could not initialize the neutral VIIPER DualSense input state.";
            return false;
        }

        streamSocket_.store(fromSocket(socket), std::memory_order_release);
        running_.store(true, std::memory_order_relaxed);
        connected_.store(true, std::memory_order_relaxed);
        try {
            feedbackThread_ = std::thread(&ViiperVirtualDualSense::feedbackLoop, this, socket);
        } catch (...) {
            streamSocket_.store(kInvalidSocket, std::memory_order_release);
            running_.store(false, std::memory_order_relaxed);
            connected_.store(false, std::memory_order_relaxed);
            closeSocket(socket);
            error = "Could not start the DualSense feedback capture thread.";
            return false;
        }
        return true;
    }

    void feedbackLoop(SOCKET socket) noexcept {
        std::array<std::uint8_t, 3> header{};
        std::array<std::uint8_t, 64> payload{};
        BufferedSocketReader reader(socket);
        std::optional<DualSenseFeedback> pendingAudio;
        auto audioWindowStarted = std::chrono::steady_clock::time_point{};

        const auto deliver = [this](const DualSenseFeedback& feedback) noexcept {
            if (!handler_) return;
            try {
                handler_(feedback);
            } catch (...) {
                // A consumer callback must never tear down the capture thread.
            }
        };
        const auto flushAudio = [this, &pendingAudio, &deliver]() noexcept {
            if (!pendingAudio) return;
            deliver(*pendingAudio);
            audioHapticsDelivered_.fetch_add(1, std::memory_order_relaxed);
            pendingAudio.reset();
        };

        while (running_.load(std::memory_order_relaxed)) {
            if (!reader.hasBufferedData()) {
                const int readable = waitForReadable(
                    socket, pendingAudio ? std::chrono::milliseconds(5)
                                         : std::chrono::milliseconds(250));
                if (readable == 0) {
                    flushAudio();
                    continue;
                }
                if (readable == SOCKET_ERROR) break;
            }
            if (!reader.readExact(header.data(), header.size())) {
                break;
            }

            const std::uint8_t frameType = header[0];
            const std::uint16_t payloadSize = static_cast<std::uint16_t>(header[1]) |
                                              (static_cast<std::uint16_t>(header[2]) << 8);
            if (payloadSize == 0 || payloadSize > payload.size()) {
                malformedFrames_.fetch_add(1, std::memory_order_relaxed);
                break;
            }
            if (!reader.readExact(payload.data(), payloadSize)) {
                break;
            }

            if (frameType != 0x01 && frameType != 0x02) {
                unknownFrames_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            DualSenseFeedback feedback{};
            if (!decodeViiperFeedbackFrame(frameType,
                                           std::span<const std::uint8_t>(payload.data(), payloadSize),
                                           feedback)) {
                malformedFrames_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            if (feedback.kind == FeedbackKind::HidOutput) {
                outputReports_.fetch_add(1, std::memory_order_relaxed);
                if (feedback.hasTriggerEffect()) {
                    triggerReports_.fetch_add(1, std::memory_order_relaxed);
                }
                // Some games leave non-zero motor bytes in reports that do
                // not enable either rumble mode. Count actionable requests,
                // not stale payload bytes, so telemetry matches the router.
                if (feedback.requestsRumbleUpdate()) {
                    rumbleReports_.fetch_add(1, std::memory_order_relaxed);
                }
                // Trigger and conventional rumble requests remain immediate.
                deliver(feedback);
            } else {
                audioHapticsFrames_.fetch_add(1, std::memory_order_relaxed);
                constexpr auto kAudioWindow = std::chrono::milliseconds(5);
                const auto now = std::chrono::steady_clock::now();
                if (!pendingAudio) {
                    pendingAudio = feedback;
                    audioWindowStarted = now;
                } else if (now - audioWindowStarted < kAudioWindow) {
                    pendingAudio->audioSequence = feedback.audioSequence;
                    pendingAudio->leftEnergy =
                        (std::max)(pendingAudio->leftEnergy, feedback.leftEnergy);
                    pendingAudio->rightEnergy =
                        (std::max)(pendingAudio->rightEnergy, feedback.rightEnergy);
                    pendingAudio->leftPeak =
                        (std::max)(pendingAudio->leftPeak, feedback.leftPeak);
                    pendingAudio->rightPeak =
                        (std::max)(pendingAudio->rightPeak, feedback.rightPeak);
                    pendingAudio->leftTransient =
                        (std::max)(pendingAudio->leftTransient, feedback.leftTransient);
                    pendingAudio->rightTransient =
                        (std::max)(pendingAudio->rightTransient, feedback.rightTransient);
                    audioHapticsCoalesced_.fetch_add(1, std::memory_order_relaxed);
                } else {
                    flushAudio();
                    pendingAudio = feedback;
                    audioWindowStarted = now;
                }
            }
        }
        flushAudio();
        connected_.store(false, std::memory_order_relaxed);
    }

    void removeDeviceAndBus() {
        if (busId_ != 0 && !deviceId_.empty()) {
            std::ostringstream path;
            path << "bus/" << busId_ << "/remove";
            std::string response;
            (void)request(path.str(), deviceId_, response);
            deviceId_.clear();
        }
        if (busId_ != 0) {
            std::string response;
            (void)request("bus/remove", std::to_string(busId_), response);
            busId_ = 0;
        }
    }

    VirtualDualSenseOptions options_;
    FeedbackHandler handler_;
    std::mutex inputWriteMutex_;
    std::atomic<std::uintptr_t> streamSocket_{kInvalidSocket};
    std::atomic_bool running_{false};
    std::thread feedbackThread_;

    std::uint32_t busId_ = 0;
    std::string deviceId_;
    std::string backendVersion_;
    std::filesystem::path logPath_;
    HANDLE serverProcess_ = nullptr;
    bool spawnedServer_ = false;

    std::atomic_bool connected_{false};
    std::atomic_uint64_t inputUpdates_{0};
    std::atomic_uint64_t outputReports_{0};
    std::atomic_uint64_t triggerReports_{0};
    std::atomic_uint64_t rumbleReports_{0};
    std::atomic_uint64_t audioHapticsFrames_{0};
    std::atomic_uint64_t audioHapticsDelivered_{0};
    std::atomic_uint64_t audioHapticsCoalesced_{0};
    std::atomic_uint64_t malformedFrames_{0};
    std::atomic_uint64_t unknownFrames_{0};
    std::uint64_t initializationBootstrapUs_ = 0;
    std::uint64_t initializationServerUs_ = 0;
    std::uint64_t initializationBusUs_ = 0;
    std::uint64_t initializationDeviceUs_ = 0;
    std::uint64_t initializationFeedbackUs_ = 0;
    std::uint64_t initializationInputUs_ = 0;
};

class AutoVirtualDualSense final : public VirtualDualSense {
public:
    explicit AutoVirtualDualSense(VirtualDualSenseOptions options)
        : options_(std::move(options)) {}

    ~AutoVirtualDualSense() override {
        close();
    }

    bool open(std::string& error, FeedbackHandler handler) override {
        close();
        // A closed backend is intentionally retained so callers can read its
        // final counters. Drop it only when starting a new session.
        active_.reset();

        std::string integratedError;
        if (options_.backend != VirtualDualSenseBackend::Sidecar) {
            auto integrated = createLibViiperVirtualDualSense(options_);
            if (integrated->open(integratedError, handler)) {
                active_ = std::move(integrated);
                error.clear();
                return true;
            }
            if (options_.backend == VirtualDualSenseBackend::Integrated) {
                error = std::move(integratedError);
                return false;
            }
        }

        auto sidecar = std::make_unique<ViiperVirtualDualSense>(options_);
        std::string sidecarError;
        if (sidecar->open(sidecarError, std::move(handler))) {
            active_ = std::move(sidecar);
            error.clear();
            return true;
        }

        if (!integratedError.empty()) {
            error = "Integrated VIIPER was unavailable (" + integratedError +
                    "). Sidecar fallback also failed: " + sidecarError;
        } else {
            error = std::move(sidecarError);
        }
        return false;
    }

    void close() noexcept override {
        if (active_) {
            active_->close();
        }
    }

    bool updateInput(const DualSenseInputState& state, std::string& error) override {
        if (!active_) {
            error = "Virtual DualSense backend is not open.";
            return false;
        }
        return active_->updateInput(state, error);
    }

    [[nodiscard]] bool connected() const noexcept override {
        return active_ && active_->connected();
    }

    VirtualDualSenseStats stats() const override {
        return active_ ? active_->stats() : VirtualDualSenseStats{};
    }

private:
    VirtualDualSenseOptions options_;
    std::unique_ptr<VirtualDualSense> active_;
};

} // namespace

std::unique_ptr<VirtualDualSense> createVirtualDualSense(VirtualDualSenseOptions options) {
    return std::make_unique<AutoVirtualDualSense>(std::move(options));
}

} // namespace asb::dualsense
