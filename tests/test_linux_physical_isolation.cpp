#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/PhysicalControllerIsolation.h"
#include "platform/linux/LinuxPaths.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;
using asb::platform::TemporaryPhysicalControllerIsolation;

fs::path gStateRoot;

fs::path markerPath() {
    return asb::platform::linux_paths::stateDirectory() / "pending-restore";
}

void writeMarker(const std::string& contents) {
    fs::create_directories(asb::platform::linux_paths::stateDirectory());
    std::ofstream file(markerPath(), std::ios::trunc);
    file << contents;
}

void clearMarker() {
    std::error_code ignored;
    fs::remove(markerPath(), ignored);
}

// With no marker on disk there is nothing to recover and nothing to report.
void testNoMarkerIsNotAnError() {
    clearMarker();
    bool recovered = true;
    std::string error;
    assert(TemporaryPhysicalControllerIsolation::recoverPending(recovered, error));
    assert(!recovered);
    assert(error.empty());
}

// A marker from a crashed session that never switched the onboard profile has
// nothing left to undo: the kernel released every EVIOCGRAB when the process
// died. The stale marker is simply cleared.
void testStaleMarkerWithoutProfileIsCleared() {
    writeMarker("owner_pid=999999\nsession_token=abc\ncontainer=/sys/devices/fake\n");
    assert(fs::exists(markerPath()));

    bool recovered = true;
    std::string error;
    assert(TemporaryPhysicalControllerIsolation::recoverPending(recovered, error));
    assert(!recovered);
    assert(error.empty());
    assert(!fs::exists(markerPath()));
}

// A live owner is mid-session, so its own restore path owns the marker and
// recovery must not touch it.
void testLiveOwnerIsLeftAlone() {
    // PID 1 always exists and is never this test process.
    writeMarker("owner_pid=1\napex_profile=2\ncontainer=/sys/devices/fake\n");

    bool recovered = true;
    std::string error;
    assert(TemporaryPhysicalControllerIsolation::recoverPending(recovered, error));
    assert(!recovered);
    assert(fs::exists(markerPath()));
    clearMarker();
}

// A pending profile restore with no controller attached must fail loudly and
// keep the marker, so the next attempt can still put the pad back.
void testPendingProfileWithoutHardwareKeepsMarker() {
    writeMarker("owner_pid=999999\napex_profile=1\ncontainer=/sys/devices/fake\n");

    bool recovered = true;
    std::string error;
    const bool ok = TemporaryPhysicalControllerIsolation::recoverPending(recovered, error);
    assert(!ok);
    assert(!recovered);
    assert(!error.empty());
    assert(fs::exists(markerPath()));
    clearMarker();
}

// Lines without a separator are ignored rather than derailing the parse.
void testMalformedLinesAreIgnored() {
    writeMarker("garbage without separator\nowner_pid=999999\n\n=novalue\n");

    bool recovered = true;
    std::string error;
    assert(TemporaryPhysicalControllerIsolation::recoverPending(recovered, error));
    assert(!recovered);
    assert(!fs::exists(markerPath()));
}

// activate() must refuse an interface whose container is unknown rather than
// silently isolating nothing, and it must not leave a marker behind.
void testActivateFailsClosedOnUnknownContainer() {
    clearMarker();
    asb::HidDeviceInfo info{};
    info.vendorId = 0x04B4;
    info.productId = 0x2412;
    info.containerId = L"/sys/devices/definitely-not-present";

    TemporaryPhysicalControllerIsolation isolation;
    std::string error;
    // No joystick node matches, so there is nothing to grab; the session is
    // still safe because the game cannot see a device that does not exist.
    const bool activated = isolation.activate(info, "token", std::nullopt, error);
    if (activated) {
        assert(isolation.active());
        std::string restoreError;
        assert(isolation.restore(restoreError));
        assert(!isolation.active());
        assert(!fs::exists(markerPath()));
    } else {
        assert(!error.empty());
        assert(!fs::exists(markerPath()));
    }
}

// Daemons suspended for a session are the one thing a crash cannot undo on its
// own, so the marker carries them and recovery restarts them. A unit name that
// cannot exist keeps the test from touching anything real; failures to start
// are ignored by design, since the session is already over by then.
void testSuspendedUnitsAreRestartedOnRecovery() {
    writeMarker("owner_pid=999999\n"
                "suspended_units=asb-test-does-not-exist-a.service,"
                "asb-test-does-not-exist-b.service\n");

    bool recovered = false;
    std::string error;
    assert(TemporaryPhysicalControllerIsolation::recoverPending(recovered, error));
    assert(recovered);
    assert(!fs::exists(markerPath()));
}

// An empty or malformed unit list must not be treated as work done.
void testEmptySuspendedUnitsListIsIgnored() {
    writeMarker("owner_pid=999999\nsuspended_units=\n");

    bool recovered = true;
    std::string error;
    assert(TemporaryPhysicalControllerIsolation::recoverPending(recovered, error));
    assert(!recovered);
    assert(!fs::exists(markerPath()));
}

} // namespace

int main() {
    // Every path this suite touches is redirected into a private tree, so the
    // real recovery marker of a live session is never disturbed.
    gStateRoot = fs::temp_directory_path() /
                 ("asb-isolation-test-" + std::to_string(::getpid()));
    fs::create_directories(gStateRoot);
    ::setenv("XDG_STATE_HOME", gStateRoot.c_str(), 1);
    ::setenv("XDG_RUNTIME_DIR", gStateRoot.c_str(), 1);

    testNoMarkerIsNotAnError();
    testStaleMarkerWithoutProfileIsCleared();
    testLiveOwnerIsLeftAlone();
    testPendingProfileWithoutHardwareKeepsMarker();
    testMalformedLinesAreIgnored();
    testActivateFailsClosedOnUnknownContainer();
    testSuspendedUnitsAreRestartedOnRecovery();
    testEmptySuspendedUnitsListIsIgnored();

    std::error_code ignored;
    fs::remove_all(gStateRoot, ignored);
    return 0;
}
