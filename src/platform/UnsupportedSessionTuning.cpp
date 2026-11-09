#include "platform/SessionTuning.h"

namespace asb::platform {

std::unique_ptr<SessionTuning> createSessionTuning(std::string_view, std::string& error) {
    error = "Live session tuning is only supported on Windows.";
    return nullptr;
}

} // namespace asb::platform
