#ifdef NDEBUG
#undef NDEBUG
#endif

#include "tools/TriggerPositionMarkers.h"

#include <cassert>

int main() {
    using namespace asb::dualsense::button;
    asb::tools::TriggerPositionMarkers markers(kCross);
    assert(!markers.update(kCross, true).onset);
    markers.update(0, true);
    assert(markers.update(kCross, true).onset);
    assert(!markers.update(kCross, true).onset);
    markers.update(0, true);
    assert(markers.update(kCircle, true).release);
    assert(!markers.update(kCircle, true).release);
    markers.update(0, false);
    const auto neutral = markers.update(kCross | kCircle, false);
    assert(!neutral.onset && !neutral.release);
    const auto held = markers.update(kCross | kCircle, true);
    assert(!held.onset && !held.release);
    markers.update(0, true);
    const auto both = markers.update(kCross | kCircle, true);
    assert(both.onset && both.release);
    markers.update(0, true);
    const auto unrelated = markers.update(kTriangle | kSquare | kR2, true);
    assert(!unrelated.onset && !unrelated.release);
    asb::tools::MarkerTrialProgress feedback(false);
    assert(!feedback.update({false, true}));
    assert(feedback.update({true, false}));
    asb::tools::MarkerTrialProgress weapon(true);
    assert(!weapon.update({false, true}));
    assert(!weapon.update({true, false}));
    assert(!weapon.update({false, false}));
    assert(weapon.update({false, true}));
    asb::tools::MarkerTrialProgress simultaneous(true);
    assert(!simultaneous.update({true, true}));
    assert(simultaneous.update({false, true}));
    return 0;
}
