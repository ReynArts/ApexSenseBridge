#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace asb::platform::linux_paths {

// Session-scoped state that must not survive a reboot: status blocks, the
// owner lock, the SDL ignore list handed to game launchers.
[[nodiscard]] inline std::filesystem::path runtimeDirectory() {
    if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime) {
        return std::filesystem::path(runtime) / "apexsensebridge";
    }
    return std::filesystem::temp_directory_path() / "apexsensebridge";
}

// State that must survive a crash and a reboot: the pending-restore marker
// that returns the pad to its original onboard profile.
[[nodiscard]] inline std::filesystem::path stateDirectory() {
    if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state) {
        return std::filesystem::path(state) / "apexsensebridge";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local/state/apexsensebridge";
    }
    return runtimeDirectory();
}

// Creates the directory if needed. Returns false only when the path cannot be
// used at all, which every caller must treat as fail-closed.
[[nodiscard]] inline bool ensureDirectory(const std::filesystem::path& path,
                                          std::string& error) {
    std::error_code code;
    std::filesystem::create_directories(path, code);
    if (code && !std::filesystem::is_directory(path)) {
        error = "Could not create " + path.string() + ": " + code.message();
        return false;
    }
    return true;
}

} // namespace asb::platform::linux_paths
