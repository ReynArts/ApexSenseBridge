#ifdef NDEBUG
#undef NDEBUG
#endif

#include "cli/PhysicalReattach.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace std::chrono_literals;
using asb::HidDeviceInfo;
using asb::cli::PhysicalReattachBudget;
using asb::cli::PhysicalReattachSteps;
using asb::cli::planPhysicalReattach;
using asb::cli::sameControllerFamily;
using asb::cli::selectReturningController;

HidDeviceInfo makeInterface(std::wstring path, std::wstring serial = L"",
                            std::wstring container = L"", std::uint16_t product = 0x2501) {
    HidDeviceInfo info;
    info.path = std::move(path);
    info.serial = std::move(serial);
    info.containerId = std::move(container);
    info.vendorId = 0x37D7;
    info.productId = product;
    info.usagePage = 0xFF00;
    return info;
}

void testBudgetAllowsSixLossesPerWindow() {
    PhysicalReattachBudget budget;
    const auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < 6; ++index) {
        assert(budget.allows(start + std::chrono::seconds(index)));
        budget.record(start + std::chrono::seconds(index));
    }
    assert(budget.recent(start + 10s) == 6);
    assert(!budget.allows(start + 10s));
    assert(!budget.allows(start + 9min));
    assert(budget.allows(start + 10min + 1s));
    assert(budget.recent(start + 10min + 1s) == 4);
    assert(budget.allows(start + 11min));
}

void testSamePathWins() {
    const auto original = makeInterface(L"\\\\?\\HID#VID_37D7&PID_2501&MI_02#A");
    const std::vector<HidDeviceInfo> candidates{
        makeInterface(L"\\\\?\\HID#VID_37D7&PID_2501&MI_02#B"),
        makeInterface(L"\\\\?\\hid#vid_37d7&pid_2501&mi_02#a"),
    };
    assert(selectReturningController(original, candidates, false) == std::size_t{1});
    assert(selectReturningController(original, candidates, true) == std::size_t{1});
}

void testSerialAndContainerFallbacks() {
    const auto original = makeInterface(L"old", L"SER-1", L"{CONTAINER-A}");
    const std::vector<HidDeviceInfo> bySerial{
        makeInterface(L"new-a", L"SER-2", L"{CONTAINER-B}"),
        makeInterface(L"new-b", L"SER-1", L"{CONTAINER-C}"),
    };
    assert(selectReturningController(original, bySerial, false) == std::size_t{1});

    const std::vector<HidDeviceInfo> byContainer{
        makeInterface(L"new-a", L"SER-2", L"{container-b}"),
        makeInterface(L"new-b", L"SER-3", L"{container-a}"),
    };
    assert(selectReturningController(original, byContainer, false) == std::size_t{1});

    const std::vector<HidDeviceInfo> differentModel{
        makeInterface(L"new-a", L"SER-1", L"{CONTAINER-A}", 0x2503),
    };
    assert(!selectReturningController(original, differentModel, false));
}

void testSingleModelFallbackNeedsASingleController() {
    const auto original = makeInterface(L"old");
    const std::vector<HidDeviceInfo> single{makeInterface(L"new")};
    assert(selectReturningController(original, single, true) == std::size_t{0});
    assert(!selectReturningController(original, single, false));

    const std::vector<HidDeviceInfo> twoOfTheSameModel{makeInterface(L"new-a"), makeInterface(L"new-b")};
    assert(!selectReturningController(original, twoOfTheSameModel, true));

    const std::vector<HidDeviceInfo> otherModelOnly{makeInterface(L"new", L"", L"", 0x2503)};
    assert(!selectReturningController(original, otherModelOnly, true));
    assert(!selectReturningController(original, {}, true));
}

void testAnotherPadDoesNotStealTheSession() {
    const auto original = makeInterface(L"old", L"SER-1", L"{CONTAINER-A}");
    const std::vector<HidDeviceInfo> otherPad{makeInterface(L"other", L"SER-9", L"{CONTAINER-Z}")};
    assert(!selectReturningController(original, otherPad, false));
}

void testPlanAfterPowerCycle() {
    using asb::flydigi::InputTransportStatus;
    using asb::flydigi::ProfileStatus;
    const std::optional<ProfileStatus> slotTwo{ProfileStatus{1, 1, false}};
    const std::optional<ProfileStatus> slotThree{ProfileStatus{2, 2, false}};
    const std::optional<ProfileStatus> switchBank{ProfileStatus{5, 1, true}};
    InputTransportStatus rawOn;
    rawOn.controllerData = true;
    rawOn.rawData = true;
    InputTransportStatus rawOff;
    rawOff.controllerData = true;
    rawOff.rawData = false;
    InputTransportStatus controllerOff;
    controllerOff.controllerData = false;
    controllerOff.rawData = true;

    assert((planPhysicalReattach(std::nullopt, slotTwo, rawOn) == PhysicalReattachSteps{false, false}));
    assert((planPhysicalReattach(std::uint8_t{1}, slotTwo, rawOn) == PhysicalReattachSteps{false, false}));
    assert((planPhysicalReattach(std::uint8_t{1}, slotThree, rawOn) == PhysicalReattachSteps{true, false}));
    assert((planPhysicalReattach(std::uint8_t{1}, switchBank, rawOn) == PhysicalReattachSteps{true, false}));
    assert((planPhysicalReattach(std::uint8_t{1}, std::nullopt, rawOn) == PhysicalReattachSteps{true, false}));
    assert((planPhysicalReattach(std::nullopt, std::nullopt, rawOff) == PhysicalReattachSteps{false, true}));
    assert((planPhysicalReattach(std::nullopt, std::nullopt, controllerOff) == PhysicalReattachSteps{false, true}));
    assert((planPhysicalReattach(std::nullopt, std::nullopt, std::nullopt) == PhysicalReattachSteps{false, true}));
    assert((planPhysicalReattach(std::uint8_t{2}, slotTwo, rawOff) == PhysicalReattachSteps{true, true}));
}

void testFamilyCheck() {
    assert(sameControllerFamily(false, true, false, true));
    assert(sameControllerFamily(true, false, true, false));
    assert(!sameControllerFamily(true, false, false, true));
    assert(!sameControllerFamily(false, true, false, false));
}

void testLossCauseNames() {
    using asb::cli::PhysicalLossCause;
    using asb::cli::describePhysicalLossCause;
    assert(!describePhysicalLossCause(PhysicalLossCause::InputDisconnected).empty());
    assert(describePhysicalLossCause(PhysicalLossCause::InputDisconnected) !=
           describePhysicalLossCause(PhysicalLossCause::InputStale));
    assert(describePhysicalLossCause(PhysicalLossCause::FeedbackFailed) !=
           describePhysicalLossCause(PhysicalLossCause::InputFailed));
}

}

int main() {
    testBudgetAllowsSixLossesPerWindow();
    testSamePathWins();
    testSerialAndContainerFallbacks();
    testSingleModelFallbackNeedsASingleController();
    testAnotherPadDoesNotStealTheSession();
    testPlanAfterPowerCycle();
    testFamilyCheck();
    testLossCauseNames();
    std::cout << "Physical reattach tests passed\n";
    return 0;
}
