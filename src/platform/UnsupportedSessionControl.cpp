#include "platform/SessionControl.h"

namespace asb::platform {

namespace {
class UnsupportedGlobalSessionStop final : public GlobalSessionStop {
public:
    [[nodiscard]] bool stopRequested() const noexcept override { return false; }
};
} // namespace

std::unique_ptr<GlobalSessionStop> createGlobalSessionStop(std::string&) {
    return std::make_unique<UnsupportedGlobalSessionStop>();
}

bool requestGlobalSessionStop(std::chrono::milliseconds, std::string&) noexcept {
    return true;
}

std::unique_ptr<SessionControl> connectSessionControl(
    std::string_view, std::string& error) {
    error = "Playnite session control is only available on Windows.";
    return {};
}

} // namespace asb::platform
