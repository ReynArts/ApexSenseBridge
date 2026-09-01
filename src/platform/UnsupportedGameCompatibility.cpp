#include "platform/GameCompatibility.h"

namespace asb::platform {

struct TemporarySpiderMan2WgiOverride::Impl {};

TemporarySpiderMan2WgiOverride::TemporarySpiderMan2WgiOverride()
    : impl_(std::make_unique<Impl>()) {}
TemporarySpiderMan2WgiOverride::~TemporarySpiderMan2WgiOverride() = default;
bool TemporarySpiderMan2WgiOverride::activate(std::string& error) {
    error = "The Spider-Man 2 WGI compatibility override is only available on Windows.";
    return false;
}
bool TemporarySpiderMan2WgiOverride::restore(std::string&) noexcept { return true; }
bool TemporarySpiderMan2WgiOverride::active() const noexcept { return false; }
bool TemporarySpiderMan2WgiOverride::recoveredStaleOverride() const noexcept { return false; }
bool TemporarySpiderMan2WgiOverride::recoverPending(
    bool& recovered, std::string&) noexcept {
    recovered = false;
    return true;
}

} // namespace asb::platform
