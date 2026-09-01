#include "platform/SessionControl.h"

#include <algorithm>

namespace asb::platform {
namespace {

std::string objectName(std::string_view token, std::string_view suffix) {
    std::string result = "Local\\ApexSenseBridge.Session.";
    result.append(token);
    result.push_back('.');
    result.append(suffix);
    return result;
}

} // namespace

bool isValidSessionToken(std::string_view token) noexcept {
    return token.size() == 32 &&
           std::all_of(token.begin(), token.end(), [](unsigned char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f') ||
                      (character >= 'A' && character <= 'F');
           });
}

std::string sessionReadyEventName(std::string_view token) {
    return objectName(token, "Ready");
}

std::string sessionStopEventName(std::string_view token) {
    return objectName(token, "Stop");
}

std::string sessionStatusMappingName(std::string_view token) {
    return objectName(token, "Status");
}

} // namespace asb::platform
