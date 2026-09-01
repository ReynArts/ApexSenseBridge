#pragma once

#include <memory>
#include <string>

namespace asb::platform {

class TemporarySpiderMan2WgiOverride {
public:
    TemporarySpiderMan2WgiOverride();
    ~TemporarySpiderMan2WgiOverride();

    TemporarySpiderMan2WgiOverride(const TemporarySpiderMan2WgiOverride&) = delete;
    TemporarySpiderMan2WgiOverride& operator=(const TemporarySpiderMan2WgiOverride&) = delete;

    bool activate(std::string& error);
    bool restore(std::string& error) noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool recoveredStaleOverride() const noexcept;

    static bool recoverPending(bool& recovered, std::string& error) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace asb::platform
