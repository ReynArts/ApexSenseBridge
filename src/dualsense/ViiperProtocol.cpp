#include "dualsense/ViiperProtocol.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <sstream>
#include <string_view>

namespace asb::dualsense::viiper {
namespace {

std::size_t findJsonValueStart(const std::string& json, const char* key) {
    const std::string token = std::string("\"") + key + "\"";
    auto position = json.find(token);
    if (position == std::string::npos) {
        return position;
    }
    position = json.find(':', position + token.size());
    if (position == std::string::npos) {
        return position;
    }
    ++position;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) {
        ++position;
    }
    return position;
}

bool findJsonString(const std::string& json, const char* key, std::string& value) {
    auto position = findJsonValueStart(json, key);
    if (position == std::string::npos || position >= json.size() || json[position] != '"') {
        return false;
    }

    ++position;
    std::string decoded;
    bool escaped = false;
    for (; position < json.size(); ++position) {
        const char current = json[position];
        if (escaped) {
            decoded.push_back(current);
            escaped = false;
        } else if (current == '\\') {
            escaped = true;
        } else if (current == '"') {
            value = std::move(decoded);
            return true;
        } else {
            decoded.push_back(current);
        }
    }
    return false;
}

bool findJsonUint(const std::string& json, const char* key, std::uint32_t& value) {
    auto position = findJsonValueStart(json, key);
    if (position == std::string::npos || position >= json.size()) {
        return false;
    }

    std::uint64_t decoded = 0;
    bool foundDigit = false;
    while (position < json.size() && json[position] >= '0' && json[position] <= '9') {
        foundDigit = true;
        decoded = decoded * 10 + static_cast<std::uint64_t>(json[position] - '0');
        if (decoded > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        ++position;
    }
    if (!foundDigit) {
        return false;
    }
    value = static_cast<std::uint32_t>(decoded);
    return true;
}

bool parseVersion(const std::string& version, std::array<int, 3>& parts) {
    std::size_t position = (!version.empty() && (version[0] == 'v' || version[0] == 'V')) ? 1 : 0;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (position >= version.size() || !std::isdigit(static_cast<unsigned char>(version[position]))) {
            return false;
        }
        int part = 0;
        while (position < version.size() && std::isdigit(static_cast<unsigned char>(version[position]))) {
            part = part * 10 + (version[position] - '0');
            ++position;
        }
        parts[index] = part;
        if (index + 1 < parts.size()) {
            if (position >= version.size() || version[position] != '.') {
                return false;
            }
            ++position;
        }
    }
    return true;
}

int namedPatchNumber(const std::string& version, std::string_view marker) {
    std::string lowered = version;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    auto position = lowered.find(marker);
    while (position != std::string::npos && position > 0 &&
           std::isalnum(static_cast<unsigned char>(lowered[position - 1]))) {
        position = lowered.find(marker, position + marker.size());
    }
    if (position == std::string::npos) return -1;
    position += marker.size();
    if (position >= lowered.size() || !std::isdigit(static_cast<unsigned char>(lowered[position]))) {
        return -1;
    }
    int number = 0;
    while (position < lowered.size() && std::isdigit(static_cast<unsigned char>(lowered[position]))) {
        number = number * 10 + (lowered[position] - '0');
        ++position;
    }
    return number;
}

} // namespace

std::string buildRequest(const std::string& path, const std::string& payload) {
    std::string request = path;
    if (!payload.empty()) {
        request.push_back(' ');
        request += payload;
    }
    request.push_back('\0');
    return request;
}

std::string buildStreamPath(std::uint32_t busId, const std::string& deviceId) {
    std::ostringstream path;
    path << "bus/" << busId << '/' << deviceId << '\0';
    return path.str();
}

bool parsePingResponse(const std::string& json, std::string& server, std::string& version) {
    return findJsonString(json, "server", server) && findJsonString(json, "version", version);
}

bool parseBusResponse(const std::string& json, std::uint32_t& busId) {
    return findJsonUint(json, "busId", busId);
}

bool parseDeviceResponse(const std::string& json,
                         std::uint32_t& busId,
                         std::string& deviceId) {
    return findJsonUint(json, "busId", busId) && findJsonString(json, "devId", deviceId);
}

bool isUsbIpDriverMissingResponse(const std::string& json) {
    std::string lowered = json;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return lowered.find("usbip-win2 driver not found") != std::string::npos ||
           lowered.find("native ioctl auto-attach requires the usbip-win2 driver") != std::string::npos ||
           lowered.find("failed to auto-attach device") != std::string::npos;
}

bool isDualSenseCompatibleVersion(const std::string& version) {
    std::array<int, 3> parts{};
    if (!parseVersion(version, parts)) {
        return false;
    }

    // Upstream VIIPER 0.7 added a DualSense device, but its public feedback
    // stream only carries conventional rumble and LEDs. ApexSenseBridge also
    // requires the complete adaptive-trigger report plus its audio-haptics
    // extension, so a bare upstream semantic version is not sufficient proof
    // of compatibility.
    const bool legacySteamless = parts == std::array<int, 3>{0, 6, 1} &&
                                 namedPatchNumber(version, "steamless") >= 5;
    const bool apexSenseExtension = namedPatchNumber(version, "asb") >= 1;
    return legacySteamless || apexSenseExtension;
}

} // namespace asb::dualsense::viiper
