#pragma once

#include <cstdint>
#include <string>

namespace asb::dualsense::viiper {

std::string buildRequest(const std::string& path, const std::string& payload = {});
std::string buildStreamPath(std::uint32_t busId, const std::string& deviceId);

bool parsePingResponse(const std::string& json, std::string& server, std::string& version);
bool parseBusResponse(const std::string& json, std::uint32_t& busId);
bool parseDeviceResponse(const std::string& json,
                         std::uint32_t& busId,
                         std::string& deviceId);
bool isUsbIpDriverMissingResponse(const std::string& json);
bool isDualSenseCompatibleVersion(const std::string& version);

} // namespace asb::dualsense::viiper
