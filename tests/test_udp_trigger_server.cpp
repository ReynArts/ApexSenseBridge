#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include "cli/UdpTriggerServer.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

template<class Predicate>
bool waitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}
}

int main() {
    WSADATA data{};
    expect(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Winsock startup");
    const SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    expect(sender != INVALID_SOCKET, "create sender");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    expect(bind(sender, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
           "reserve an available loopback port");
    int addressSize = sizeof(address);
    expect(getsockname(sender, reinterpret_cast<sockaddr*>(&address), &addressSize) == 0,
           "get reserved port");
    const auto port = ntohs(address.sin_port);
    std::atomic_uint callbacks{0};
    asb::cli::UdpTriggerServer server("127.0.0.1", port, [&](const auto& feedback) {
        expect(feedback.enableBits1 == 0x08 && feedback.leftTriggerEffect[0] == 1,
               "decode the left trigger effect");
        callbacks.fetch_add(1);
    });
    std::string error;
    expect(!server.start(error) && !error.empty(), "busy port fails without starting worker");
    server.stop();
    closesocket(sender);
    const SOCKET traffic = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    expect(traffic != INVALID_SOCKET, "create traffic socket");
    const std::string valid = R"({"side":"left","effect":[1,0,0,0,0,0,0,0,0,0,0]})";
    const auto send = [&](const std::string& packet) {
        return sendto(traffic, packet.data(), static_cast<int>(packet.size()), 0,
                      reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    };
    expect(server.start(error), "start after releasing busy port");
    expect(server.start(error), "starting an active listener is harmless");
    expect(send(valid) > 0, "send valid packet");
    expect(waitFor([&] { return callbacks.load() == 1; }), "valid packet reaches callback");
    expect(send("invalid") > 0, "send invalid packet");
    expect(waitFor([&] { return server.stats().rejected == 1; }), "invalid packet rejected");
    expect(server.stats().accepted == 1, "invalid packet did not reach callback");
    server.stop();
    server.stop();
    const auto stoppedCount = callbacks.load();
    expect(send(valid) > 0, "send while stopped");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    expect(callbacks.load() == stoppedCount, "stop joins all callbacks");

    std::atomic_bool sending{true};
    std::thread flood([&] {
        while (sending.load()) {
            send(valid);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    for (unsigned i = 0; i < 10; ++i) {
        const auto before = callbacks.load();
        expect(server.start(error), "restart under traffic");
        expect(waitFor([&] { return callbacks.load() > before; }), "restart receives traffic");
        server.stop();
    }
    sending.store(false);
    flood.join();
    closesocket(traffic);
    WSACleanup();
    std::puts("UDP busy-port, packet validation and stop/restart tests passed.");
}
