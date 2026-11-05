#define WIN32_LEAN_AND_MEAN
#include "cli/UdpTriggerServer.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>

namespace asb::cli {
namespace {

#ifdef _WIN32
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool findString(const std::string& json, const char* key, std::string& value) {
    const std::string token = '"' + std::string(key) + '"';
    const auto keyPosition = json.find(token);
    if (keyPosition == std::string::npos) return false;
    auto position = json.find(':', keyPosition + token.size());
    if (position == std::string::npos) return false;
    ++position;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) ++position;
    if (position >= json.size() || json[position] != '"') return false;
    ++position;
    const auto end = json.find('"', position);
    if (end == std::string::npos) return false;
    value = json.substr(position, end - position);
    return true;
}

bool findUnsigned(const std::string& json, const char* key, unsigned& value) {
    const std::string token = '"' + std::string(key) + '"';
    const auto keyPosition = json.find(token);
    if (keyPosition == std::string::npos) return false;
    auto position = json.find(':', keyPosition + token.size());
    if (position == std::string::npos) return false;
    ++position;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) ++position;
    if (position >= json.size()) return false;
    char* end = nullptr;
    const auto parsed = std::strtoul(json.c_str() + position, &end, 10);
    if (end == json.c_str() + position || parsed > 255) return false;
    value = static_cast<unsigned>(parsed);
    return true;
}

bool findEffect(const std::string& json, std::array<std::uint8_t, 11>& effect) {
    const auto keyPosition = json.find("\"effect\"");
    if (keyPosition == std::string::npos) return false;
    auto position = json.find('[', keyPosition);
    const auto end = json.find(']', position);
    if (position == std::string::npos || end == std::string::npos) return false;
    ++position;
    std::size_t count = 0;
    while (position < end) {
        while (position < end && (std::isspace(static_cast<unsigned char>(json[position])) ||
                                  json[position] == ',')) ++position;
        if (position >= end) break;
        char* numberEnd = nullptr;
        const auto parsed = std::strtoul(json.c_str() + position, &numberEnd, 10);
        if (numberEnd == json.c_str() + position || parsed > 255 || count >= effect.size()) return false;
        effect[count++] = static_cast<std::uint8_t>(parsed);
        position = static_cast<std::size_t>(numberEnd - json.c_str());
    }
    return count == effect.size();
}

bool parsePacket(const std::string& packet, asb::dualsense::DualSenseFeedback& feedback) {
    std::string side;
    if (!findString(packet, "side", side) || (side != "left" && side != "right")) return false;
    std::array<std::uint8_t, 11> effect{};
    if (!findEffect(packet, effect)) return false;

    unsigned enable = side == "left" ? 0x08U : 0x04U;
    unsigned suppliedEnable = 0;
    if (findUnsigned(packet, "enable", suppliedEnable)) enable = suppliedEnable;
    if ((enable & (side == "left" ? 0x08U : 0x04U)) == 0) return false;

    feedback = {};
    feedback.kind = asb::dualsense::FeedbackKind::HidOutput;
    feedback.enableBits1 = static_cast<std::uint8_t>(enable);
    if (side == "left") feedback.leftTriggerEffect = effect;
    else feedback.rightTriggerEffect = effect;
    return true;
}
#endif

} // namespace

struct UdpTriggerServer::Impl {
    std::string bindAddress;
    std::uint16_t port;
    Handler handler;
    std::atomic_bool running{false};
    std::atomic_uint64_t packets{0};
    std::atomic_uint64_t accepted{0};
    std::atomic_uint64_t rejected{0};
#ifdef _WIN32
    SOCKET socket = INVALID_SOCKET;
    std::thread worker;
#endif
};

UdpTriggerServer::UdpTriggerServer(std::string bindAddress, std::uint16_t port, Handler handler)
    : impl_(std::make_unique<Impl>()) {
    impl_->bindAddress = std::move(bindAddress);
    impl_->port = port;
    impl_->handler = std::move(handler);
}

UdpTriggerServer::~UdpTriggerServer() {
    stop();
}

bool UdpTriggerServer::start(std::string& error) {
#ifndef _WIN32
    error = "UDP trigger input is only supported on Windows.";
    return false;
#else
    if (impl_->running.exchange(true)) return true;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        impl_->running.store(false);
        error = "WSAStartup failed.";
        return false;
    }
    impl_->socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->socket == INVALID_SOCKET) {
        WSACleanup();
        impl_->running.store(false);
        error = "Could not create UDP socket.";
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(impl_->port);
    if (inet_pton(AF_INET, impl_->bindAddress.c_str(), &address.sin_addr) != 1 ||
        bind(impl_->socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        closesocket(impl_->socket);
        impl_->socket = INVALID_SOCKET;
        WSACleanup();
        impl_->running.store(false);
        error = "Could not bind UDP trigger socket to " + impl_->bindAddress + ":" +
                std::to_string(impl_->port) + ".";
        return false;
    }
    impl_->worker = std::thread([state = impl_.get()] {
        std::array<char, 4096> buffer{};
        while (state->running.load(std::memory_order_relaxed)) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(state->socket, &readSet);
            timeval timeout{0, 100000};
            if (select(0, &readSet, nullptr, nullptr, &timeout) <= 0) continue;
            const int received = recvfrom(state->socket, buffer.data(),
                                          static_cast<int>(buffer.size()), 0, nullptr, nullptr);
            if (received <= 0) continue;
            state->packets.fetch_add(1, std::memory_order_relaxed);
            asb::dualsense::DualSenseFeedback feedback{};
            if (!parsePacket(std::string(buffer.data(), static_cast<std::size_t>(received)), feedback)) {
                state->rejected.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            state->accepted.fetch_add(1, std::memory_order_relaxed);
            try {
                state->handler(feedback);
            } catch (...) {
                state->rejected.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });
    return true;
#endif
}

void UdpTriggerServer::stop() noexcept {
#ifdef _WIN32
    if (!impl_ || !impl_->running.exchange(false)) return;
    if (impl_->socket != INVALID_SOCKET) {
        closesocket(impl_->socket);
        impl_->socket = INVALID_SOCKET;
    }
    if (impl_->worker.joinable()) impl_->worker.join();
    WSACleanup();
#endif
}

UdpTriggerServerStats UdpTriggerServer::stats() const noexcept {
    return {impl_->packets.load(), impl_->accepted.load(), impl_->rejected.load()};
}

} // namespace asb::cli
