#ifdef NDEBUG
#undef NDEBUG
#endif

#include "dualsense/FeedbackRouter.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace {

using asb::dualsense::DualSenseFeedback;
using asb::dualsense::FeedbackKind;
using asb::dualsense::FeedbackRouter;

DualSenseFeedback triggerFrame(bool right, std::uint8_t type, std::uint8_t strength) {
    DualSenseFeedback frame;
    frame.kind = FeedbackKind::HidOutput;
    frame.enableBits1 = right ? 0x04 : 0x08;
    auto& effect = right ? frame.rightTriggerEffect : frame.leftTriggerEffect;
    effect[0] = type;
    effect[1] = strength;
    return frame;
}

DualSenseFeedback lightbarFrame(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    DualSenseFeedback frame;
    frame.kind = FeedbackKind::HidOutput;
    frame.hasLightbar = true;
    frame.lightbarRed = red;
    frame.lightbarGreen = green;
    frame.lightbarBlue = blue;
    return frame;
}

struct Collector {
    std::vector<DualSenseFeedback> frames;
    FeedbackRouter::Sink sink() {
        return [this](const DualSenseFeedback& frame) { frames.push_back(frame); };
    }
};

void testForwardsWhileAttached() {
    FeedbackRouter router;
    Collector collector;
    router.attach(collector.sink());
    assert(router.attached());
    router.route(triggerFrame(true, 0x25, 40));
    router.route(triggerFrame(false, 0x21, 10));
    assert(collector.frames.size() == 2);
    assert(collector.frames[0].rightTriggerEffect[1] == 40);
}

void testDetachedFramesAreRememberedNotForwarded() {
    FeedbackRouter router;
    Collector first;
    router.attach(first.sink());
    router.route(triggerFrame(true, 0x25, 40));
    router.detach();
    assert(!router.attached());
    router.route(triggerFrame(true, 0x25, 90));
    assert(first.frames.size() == 1);

    Collector second;
    router.attach(second.sink());
    assert(second.frames.size() == 1);
    assert(second.frames[0].kind == FeedbackKind::HidOutput);
    assert((second.frames[0].enableBits1 & 0x04) != 0);
    assert((second.frames[0].enableBits1 & 0x08) == 0);
    assert(second.frames[0].rightTriggerEffect[0] == 0x25);
    assert(second.frames[0].rightTriggerEffect[1] == 90);
}

void testReplayCarriesBothSidesInOneFrame() {
    FeedbackRouter router;
    router.route(triggerFrame(true, 0x25, 40));
    router.route(triggerFrame(false, 0x21, 12));
    Collector collector;
    router.attach(collector.sink());
    assert(collector.frames.size() == 1);
    assert(collector.frames[0].enableBits1 == 0x0C);
    assert(collector.frames[0].rightTriggerEffect[1] == 40);
    assert(collector.frames[0].leftTriggerEffect[1] == 12);
}

void testEmptyEffectKeepsLastActiveEffect() {
    FeedbackRouter router;
    router.route(triggerFrame(true, 0x25, 40));
    router.route(triggerFrame(true, 0x00, 0));
    Collector collector;
    router.attach(collector.sink());
    assert(collector.frames.size() == 1);
    assert(collector.frames[0].rightTriggerEffect[0] == 0x25);
}

void testRumbleIsNeverReplayed() {
    FeedbackRouter router;
    DualSenseFeedback frame = triggerFrame(true, 0x25, 40);
    frame.enableBits1 = static_cast<std::uint8_t>(frame.enableBits1 | 0x01);
    frame.enableBits3 = 0x04;
    frame.rumbleLeft = 255;
    frame.rumbleRight = 255;
    router.route(frame);
    Collector collector;
    router.attach(collector.sink());
    assert(collector.frames.size() == 1);
    assert(!collector.frames[0].requestsRumbleUpdate());
    assert(collector.frames[0].rumbleLeft == 0 && collector.frames[0].rumbleRight == 0);
}

void testLightbarReplayedAsItsOwnFrame() {
    FeedbackRouter router;
    router.route(lightbarFrame(1, 2, 3));
    router.route(lightbarFrame(10, 20, 30));
    Collector collector;
    router.attach(collector.sink());
    assert(collector.frames.size() == 1);
    assert(collector.frames[0].hasLightbarColor());
    assert(collector.frames[0].lightbarRed == 10);
    assert(collector.frames[0].lightbarGreen == 20);
    assert(collector.frames[0].lightbarBlue == 30);
    assert(collector.frames[0].enableBits1 == 0);
}

void testAudioFramesAreForwardedButNotRemembered() {
    FeedbackRouter router;
    Collector collector;
    router.attach(collector.sink());
    DualSenseFeedback audio;
    audio.kind = FeedbackKind::AudioHaptics;
    audio.leftEnergy = 500;
    router.route(audio);
    assert(collector.frames.size() == 1);
    router.detach();
    Collector later;
    router.attach(later.sink());
    assert(later.frames.empty());
}

void testNothingRememberedMeansNothingReplayed() {
    FeedbackRouter router;
    Collector collector;
    router.attach(collector.sink());
    assert(collector.frames.empty());
}

void testDetachWaitsForInFlightRoute() {
    FeedbackRouter router;
    std::atomic<bool> inside{false};
    std::atomic<bool> release{false};
    std::atomic<bool> finished{false};
    router.attach([&](const DualSenseFeedback&) {
        inside.store(true);
        while (!release.load()) std::this_thread::yield();
        finished.store(true);
    });
    std::thread producer([&] { router.route(triggerFrame(true, 0x25, 1)); });
    while (!inside.load()) std::this_thread::yield();
    std::thread detacher([&] { router.detach(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!finished.load());
    release.store(true);
    detacher.join();
    producer.join();
    assert(finished.load());
    assert(!router.attached());
}

}

int main() {
    testForwardsWhileAttached();
    testDetachedFramesAreRememberedNotForwarded();
    testReplayCarriesBothSidesInOneFrame();
    testEmptyEffectKeepsLastActiveEffect();
    testRumbleIsNeverReplayed();
    testLightbarReplayedAsItsOwnFrame();
    testAudioFramesAreForwardedButNotRemembered();
    testNothingRememberedMeansNothingReplayed();
    testDetachWaitsForInFlightRoute();
    std::cout << "Feedback router tests passed\n";
    return 0;
}
