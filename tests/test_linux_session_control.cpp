#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/SessionControl.h"
#include "platform/linux/LinuxPaths.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace asb::platform;

const std::string kToken = "0123456789abcdef0123456789abcdef";

fs::path sessionPath(const std::string& name) {
    return linux_paths::runtimeDirectory() / name;
}

void testTokenValidation() {
    assert(isValidSessionToken(kToken));
    assert(!isValidSessionToken("short"));
    assert(!isValidSessionToken("0123456789abcdef0123456789abcdeg"));
    assert(!isValidSessionToken(""));
}

// On Linux the three objects are plain file names inside the session runtime
// directory, with no Windows kernel-namespace prefix.
void testObjectNamesAreFileNames() {
    const auto ready = sessionReadyEventName(kToken);
    const auto stop = sessionStopEventName(kToken);
    const auto status = sessionStatusMappingName(kToken);
    assert(ready == kToken + ".Ready");
    assert(stop == kToken + ".Stop");
    assert(status == kToken + ".Status");
    assert(ready.find('\\') == std::string::npos);
}

void testStatusBlockRoundTrip() {
    std::string error;
    auto control = connectSessionControl(kToken, error);
    assert(control);
    assert(error.empty());

    const auto statusFile = sessionPath(sessionStatusMappingName(kToken));
    assert(fs::exists(statusFile));
    assert(fs::file_size(statusFile) == kSessionStatusSize);
    // The ready FIFO must be a real FIFO, not a regular file.
    struct stat info {};
    assert(::stat(sessionPath(sessionReadyEventName(kToken)).c_str(), &info) == 0);
    assert(S_ISFIFO(info.st_mode));

    assert(control->publish(SessionPhase::Ready, 0, "bridge is live", error));

    // An independent reader must see exactly what was published.
    SessionStatusBlock block{};
    std::ifstream reader(statusFile, std::ios::binary);
    reader.read(reinterpret_cast<char*>(&block), sizeof(block));
    assert(reader.gcount() == static_cast<std::streamsize>(kSessionStatusSize));
    assert(block.magic == kSessionStatusMagic);
    assert(block.protocolVersion == kSessionProtocolVersion);
    assert(block.phase == static_cast<std::uint16_t>(SessionPhase::Ready));
    assert(block.exitCode == 0);
    assert(block.messageLength == std::strlen("bridge is live"));
    assert(std::string(block.message.data(), block.messageLength) == "bridge is live");

    // A failure phase carries its exit code and message the same way.
    assert(control->publish(SessionPhase::Failed, 11, "isolation failed", error));
    std::ifstream second(statusFile, std::ios::binary);
    second.read(reinterpret_cast<char*>(&block), sizeof(block));
    assert(block.phase == static_cast<std::uint16_t>(SessionPhase::Failed));
    assert(block.exitCode == 11);

    // Nobody holds the read end, so readiness is a no-op rather than a hang.
    assert(control->signalReady(error));
    // No stop file exists yet.
    assert(!control->stopRequested());
    { std::ofstream stop(sessionPath(sessionStopEventName(kToken))); }
    assert(control->stopRequested());
}

// A status file that exists but belongs to something else must be refused
// rather than silently reinterpreted.
void testForeignStatusBlockIsRejected() {
    const std::string token = "ffffffffffffffffffffffffffffffff";
    const auto path = sessionPath(sessionStatusMappingName(token));
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        std::vector<char> junk(kSessionStatusSize, 0);
        junk[0] = 0x7F;
        junk[1] = 0x45;
        file.write(junk.data(), static_cast<std::streamsize>(junk.size()));
    }

    std::string error;
    auto control = connectSessionControl(token, error);
    assert(!control);
    assert(!error.empty());
}

void testInvalidTokenIsRefused() {
    std::string error;
    auto control = connectSessionControl("not-a-token", error);
    assert(!control);
    assert(!error.empty());
}

// The owner lock admits one session at a time and releases on destruction.
void testGlobalStopLockContention() {
    std::string error;
    auto first = createGlobalSessionStop(error);
    assert(first);
    assert(error.empty());

    std::string secondError;
    auto second = createGlobalSessionStop(secondError);
    assert(!second);
    assert(secondError.find("already active") != std::string::npos);

    // A stop request against a live owner signals it; the owner here is this
    // very process, so only verify that an unheld lock reports success.
    first.reset();
    std::string stopError;
    assert(requestGlobalSessionStop(std::chrono::milliseconds(500), stopError));

    auto third = createGlobalSessionStop(error);
    assert(third);
}

} // namespace

int main() {
    const auto root = fs::temp_directory_path() /
                      ("asb-session-test-" + std::to_string(::getpid()));
    fs::create_directories(root);
    ::setenv("XDG_RUNTIME_DIR", root.c_str(), 1);
    ::setenv("XDG_STATE_HOME", root.c_str(), 1);

    testTokenValidation();
    testObjectNamesAreFileNames();
    testStatusBlockRoundTrip();
    testForeignStatusBlockIsRejected();
    testInvalidTokenIsRefused();
    testGlobalStopLockContention();

    std::error_code ignored;
    fs::remove_all(root, ignored);
    return 0;
}
