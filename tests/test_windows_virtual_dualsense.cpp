#ifdef NDEBUG
#undef NDEBUG
#endif

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include "dualsense/VirtualDualSense.h"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

bool sendAll(SOCKET socket, const std::uint8_t* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const int result = send(socket,
                                reinterpret_cast<const char*>(data + sent),
                                static_cast<int>(size - sent),
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
        const int result = recv(socket,
                                reinterpret_cast<char*>(data + received),
                                static_cast<int>(size - received),
                                0);
        if (result <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(result);
    }
    return true;
}

bool receiveNullTerminated(SOCKET socket, std::string& request) {
    request.clear();
    for (;;) {
        char character = 0;
        const int result = recv(socket, &character, 1, 0);
        if (result != 1) {
            return false;
        }
        if (character == '\0') {
            return true;
        }
        request.push_back(character);
        if (request.size() > 4096) {
            return false;
        }
    }
}

SOCKET acceptClient(SOCKET listener) {
    return accept(listener, nullptr, nullptr);
}

bool serveRequest(SOCKET listener,
                  const std::string& expected,
                  const std::string& response,
                  std::vector<std::string>& seen) {
    const SOCKET client = acceptClient(listener);
    if (client == INVALID_SOCKET) {
        return false;
    }
    std::string request;
    const bool received = receiveNullTerminated(client, request);
    seen.push_back(request);
    const bool success = received && request == expected && sendAll(client, response);
    shutdown(client, SD_BOTH);
    closesocket(client);
    return success;
}

void fakeViiperServer(std::promise<std::uint16_t> ready,
                      std::promise<bool> finished,
                      std::vector<std::string>& seen) {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        ready.set_value(0);
        finished.set_value(false);
        return;
    }

    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        ready.set_value(0);
        finished.set_value(false);
        WSACleanup();
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool listening = bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
                     listen(listener, 4) == 0;
    int addressLength = sizeof(address);
    if (listening && getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                                 &addressLength) != 0) {
        listening = false;
    }
    ready.set_value(listening ? ntohs(address.sin_port) : 0);
    if (!listening) {
        closesocket(listener);
        finished.set_value(false);
        WSACleanup();
        return;
    }

    bool success = true;
    success = success && serveRequest(
        listener,
        "ping",
        R"({"server":"VIIPER","version":"v0.6.1-steamless8"})",
        seen);
    success = success && serveRequest(listener, "bus/create 0", R"({"busId":7})", seen);
    success = success && serveRequest(
        listener,
        R"(bus/7/add {"type":"dualsense"})",
        R"({"busId":7,"devId":"virtual-ds5"})",
        seen);

    const SOCKET stream = success ? acceptClient(listener) : INVALID_SOCKET;
    if (stream == INVALID_SOCKET) {
        success = false;
    } else {
        std::string streamPath;
        std::array<std::uint8_t, 33> neutral{};
        success = receiveNullTerminated(stream, streamPath) &&
                  streamPath == "bus/7/virtual-ds5" &&
                  receiveExact(stream, neutral.data(), neutral.size()) &&
                  neutral[0] == 0x80 && neutral[1] == 0x80 &&
                  neutral[2] == 0x80 && neutral[3] == 0x80 &&
                  neutral[4] == 0 && neutral[5] == 0 && neutral[31] == 100;
        seen.push_back(streamPath);

        std::array<std::uint8_t, 33> proxied{};
        success = success && receiveExact(stream, proxied.data(), proxied.size()) &&
                  proxied[0] == 0x11 && proxied[1] == 0x22 &&
                  proxied[4] == 0x55 && proxied[5] == 0x66 &&
                  proxied[7] == 0x20;

        std::array<std::uint8_t, 27> hid{};
        hid[0] = 0x01;
        hid[2] = 9;
        hid[5] = 0x21;
        const std::array<std::uint8_t, 3> hidHeader{0x01, 27, 0};
        const std::array<std::uint8_t, 16> audio{};
        const std::array<std::uint8_t, 3> audioHeader{0x02, 16, 0};
        success = success && sendAll(stream, hidHeader.data(), hidHeader.size()) &&
                  sendAll(stream, hid.data(), hid.size()) &&
                  sendAll(stream, audioHeader.data(), audioHeader.size()) &&
                  sendAll(stream, audio.data(), audio.size());

        char ignored = 0;
        while (recv(stream, &ignored, 1, 0) > 0) {}
        closesocket(stream);
    }

    success = success && serveRequest(listener,
                                      "bus/7/remove virtual-ds5",
                                      R"({"ok":true})",
                                      seen);
    success = success && serveRequest(listener,
                                      "bus/remove 7",
                                      R"({"ok":true})",
                                      seen);

    closesocket(listener);
    WSACleanup();
    finished.set_value(success);
}

} // namespace

int main() {
    std::promise<std::uint16_t> readyPromise;
    auto ready = readyPromise.get_future();
    std::promise<bool> finishedPromise;
    auto finished = finishedPromise.get_future();
    std::vector<std::string> seen;
    std::thread server(fakeViiperServer,
                       std::move(readyPromise),
                       std::move(finishedPromise),
                       std::ref(seen));
    const auto apiPort = ready.get();
    assert(apiPort != 0);

    std::atomic_uint64_t callbacks{0};
    asb::dualsense::VirtualDualSenseOptions options{};
    options.apiPort = apiPort;
    // Auto mode must retain the existing sidecar path when the optional
    // in-process library is absent or intentionally unavailable.
    options.viiperLibrary = L"definitely-missing-libVIIPER.dll";
    auto virtualDualSense = asb::dualsense::createVirtualDualSense(options);
    std::string error;
    const bool opened = virtualDualSense->open(
        error,
        [&callbacks](const asb::dualsense::DualSenseFeedback&) {
            callbacks.fetch_add(1, std::memory_order_relaxed);
        });
    assert(opened);
    assert(error.empty());

    asb::dualsense::DualSenseInputState input{};
    input.lx = 0x11;
    input.ly = 0x22;
    input.l2 = 0x55;
    input.r2 = 0x66;
    input.buttons = 0x0020;
    assert(virtualDualSense->updateInput(input, error));
    assert(error.empty());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    asb::dualsense::VirtualDualSenseStats stats{};
    do {
        stats = virtualDualSense->stats();
        if (stats.outputReports == 1 &&
            stats.audioHapticsFrames == 1 &&
            stats.audioHapticsDelivered == 1 &&
            callbacks.load(std::memory_order_relaxed) == 2) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);

    assert(stats.connected);
    assert(stats.backendVersion == "v0.6.1-steamless8");
    assert(stats.inputUpdates == 1);
    assert(stats.outputReports == 1);
    assert(stats.triggerReports == 1);
    assert(stats.rumbleReports == 1);
    assert(stats.audioHapticsFrames == 1);
    assert(stats.audioHapticsDelivered == 1);
    assert(stats.audioHapticsCoalesced == 0);
    assert(stats.malformedFrames == 0);
    assert(stats.unknownFrames == 0);
    assert(callbacks.load(std::memory_order_relaxed) == 2);

    virtualDualSense->close();
    const auto closedStats = virtualDualSense->stats();
    assert(!closedStats.connected);
    assert(closedStats.backendVersion == "v0.6.1-steamless8");
    assert(closedStats.inputUpdates == 1);
    assert(closedStats.outputReports == 1);
    assert(closedStats.triggerReports == 1);
    assert(closedStats.rumbleReports == 1);
    assert(closedStats.audioHapticsFrames == 1);
    assert(closedStats.audioHapticsDelivered == 1);
    assert(finished.get());
    server.join();

    assert(seen.size() == 6);
    assert(seen[0] == "ping");
    assert(seen[1] == "bus/create 0");
    assert(seen[2] == R"(bus/7/add {"type":"dualsense"})");
    assert(seen[3] == "bus/7/virtual-ds5");
    assert(seen[4] == "bus/7/remove virtual-ds5");
    assert(seen[5] == "bus/remove 7");
    return 0;
}
