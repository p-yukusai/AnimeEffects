// Contract tests for the per-channel track model's storage/sampling primitive:
// core::ChannelTrack and core::ChannelSpec.
//
// What is pinned, and why:
//   1 the channel metadata (default, clamp, integral storage, step-held) is the
//     source the timeline, graph, inspector and blender will all read;
//   2 no keys means the base value; one key holds; two keys interpolate with the
//     FROM key's easing, exactly as the old gatherer/blender pair did;
//   3 each track is independent, so X and Y can be keyed at different frames;
//   4 the Image offset is step-held and does not loop (blendImageKey's rule);
//   5 looped channels evaluate the wrapped segment from the last key back to the
//     first, and hold outside the keys when loop is off;
//   6 values are clamped/quantized by the channel spec, and keys carry stable
//     ids that a replacement preserves.
//
// Headless: no QApplication, no widgets, no GL. ChannelTrack.cpp,
// ChannelSpec.cpp and KeyId.cpp are compiled directly so this gate does not drag
// in core::Project and the GUI-side audio stub the full core library needs.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>

#include "XCAssert.h"
#include "core/ChannelSpec.h"
#include "core/ChannelTrack.h"
#include "core/MoveStateKey.h"
#include "core/PropertyTrack.h"
#include "core/StateTrack.h"
#include "core/TimeInfo.h"
#include "core/TimeKeyType.h"
#include "util/Easing.h"
#include "util/Range.h"

namespace {

// The core library leaves these two sinks undefined; a test that links the
// serializers (MoveStateKey pulls them in) supplies them, aborting on failure.
class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};

class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};

HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;

int failures = 0;

void check(bool aOk, const char* aWhat) {
    if (!aOk) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

bool near(float aLhs, float aRhs, float aEpsilon = 1e-5f) {
    return std::fabs(aLhs - aRhs) <= aEpsilon;
}

core::TimeInfo info(int aFrame, int aMin, int aMax, bool aLoop) {
    core::TimeInfo time;
    time.frame = core::Frame(aFrame);
    time.frameRange = util::Range(aMin, aMax);
    time.loop = aLoop;
    return time;
}

// ---------------------------------------------------------------------------
// 1. Channel metadata.
// ---------------------------------------------------------------------------
void sectionSpecs() {
    const core::ChannelSpec moveX = core::channelSpec(core::TimeKeyType_Move, 0);
    check(moveX.valid && moveX.graphable, "Move X is a graphable channel");
    check(near(moveX.defaultValue, 0.0f), "Move's default position is 0");
    check(moveX.interpolation == core::ChannelInterpolation::Eased, "Move X is eased");

    const core::ChannelSpec scaleY = core::channelSpec(core::TimeKeyType_Scale, 1);
    check(near(scaleY.defaultValue, 1.0f), "Scale's default is 1");

    const core::ChannelSpec opa = core::channelSpec(core::TimeKeyType_Opacity, 0);
    check(near(opa.defaultValue, 1.0f) && near(opa.minValue, 0.0f) && near(opa.maxValue, 1.0f),
          "Opacity defaults to 1 and clamps to [0,1]");

    const core::ChannelSpec hsvSat = core::channelSpec(core::TimeKeyType_HSV, 1);
    check(hsvSat.integral, "HSV channels store integers");
    check(near(hsvSat.defaultValue, 100.0f), "HSV saturation defaults to 100");

    const core::ChannelSpec waveMix = core::channelSpec(core::TimeKeyType_Wave, 4);
    check(near(waveMix.minValue, 0.0f) && near(waveMix.maxValue, 1.0f), "Wave mix clamps to [0,1]");
    check(near(waveMix.defaultValue, 0.35f), "Wave mix keeps its preset default");

    const core::ChannelSpec imageX = core::channelSpec(core::TimeKeyType_Image, 0);
    check(imageX.interpolation == core::ChannelInterpolation::Step, "Image offset is step-held");

    const core::ChannelSpec centroidX = core::channelSpec(core::TimeKeyType_Move, 2);
    check(centroidX.valid && !centroidX.graphable, "Move's centroid is a channel but not a graph channel");
    check(core::propertyChannelCount(core::TimeKeyType_Move) == 4, "Move has four channel tracks");
    check(!core::channelSpec(core::TimeKeyType_Move, 4).valid, "Move has no fifth channel");
    check(!core::channelSpec(core::TimeKeyType_Bone, 0).valid, "Bone is structural, not a channel");
    check(core::hasChannelTracks(core::TimeKeyType_Move), "Move publishes channel tracks");
    check(!core::hasChannelTracks(core::TimeKeyType_Mesh), "Mesh does not publish channel tracks");

    std::printf("[1] channel specs: defaults, clamps, step-held and structural types ok\n");
}

// ---------------------------------------------------------------------------
// 2. Base value, single key hold, and interpolation.
// ---------------------------------------------------------------------------
void sectionInterpolation() {
    core::ChannelTrack scale(core::TimeKeyType_Scale, 0);
    check(near(scale.baseValue(), 1.0f), "a Scale track starts at its default");
    check(near(scale.sample(info(5, 0, 10, false)), 1.0f), "no keys means the base value");

    scale.insertKey(0, 1.0f);
    check(near(scale.sample(info(5, 0, 10, false)), 1.0f), "one key holds before it");
    check(near(scale.sample(info(10, 0, 10, false)), 1.0f), "one key holds after it");

    scale.insertKey(10, 3.0f);
    check(near(scale.sample(info(5, 0, 10, false)), 2.0f), "linear interpolation at the midpoint");

    util::Easing::Param quadIn;
    quadIn.type = util::Easing::Type_Quad;
    quadIn.range = util::Easing::Range_In;
    core::ChannelTrack eased(core::TimeKeyType_Scale, 0);
    eased.insertKey(0, 0.0f, quadIn);
    eased.insertKey(10, 10.0f);
    check(near(eased.sample(info(5, 0, 10, false)), 2.5f), "the FROM key's easing shapes the segment");

    std::printf("[2] interpolation: base, hold and eased segment ok\n");
}

// ---------------------------------------------------------------------------
// 3. Independent channels.
// ---------------------------------------------------------------------------
void sectionIndependentChannels() {
    core::ChannelTrack x(core::TimeKeyType_Move, 0);
    core::ChannelTrack y(core::TimeKeyType_Move, 1);

    x.insertKey(0, 0.0f);
    x.insertKey(10, 10.0f);
    y.insertKey(0, 0.0f);
    y.insertKey(20, 20.0f);

    check(near(x.sample(info(5, 0, 20, false)), 5.0f), "X samples its own keys");
    check(near(y.sample(info(5, 0, 20, false)), 5.0f), "Y samples its own keys");
    check(near(x.sample(info(10, 0, 20, false)), 10.0f), "X is keyed at 10");
    check(near(y.sample(info(10, 0, 20, false)), 10.0f), "Y is interpolated at 10");
    check(near(y.sample(info(15, 0, 20, false)), 15.0f), "Y keeps its own timing");
    check(near(x.sample(info(15, 0, 20, false)), 10.0f), "X holds after its last key");

    std::printf("[3] independent channels: X and Y keep different key frames\n");
}

// ---------------------------------------------------------------------------
// 4. Step-held channels.
// ---------------------------------------------------------------------------
void sectionStepHeld() {
    core::ChannelTrack offset(core::TimeKeyType_Image, 0);
    offset.insertKey(10, 10.0f);
    offset.insertKey(20, 20.0f);

    check(near(offset.sample(info(5, 0, 30, true)), 0.0f), "Image offset before the first key is the base");
    check(near(offset.sample(info(10, 0, 30, true)), 10.0f), "Image offset reads its exact key");
    check(near(offset.sample(info(15, 0, 30, true)), 10.0f), "Image offset holds the previous key");
    check(near(offset.sample(info(25, 0, 30, true)), 20.0f), "Image offset holds after the last key");
    check(near(offset.sample(info(5, 0, 30, true)), 0.0f), "Image offset never loops");

    std::printf("[4] step-held channel: last key or base, never interpolated\n");
}

// ---------------------------------------------------------------------------
// 5. Loop and hold.
// ---------------------------------------------------------------------------
void sectionLoop() {
    core::ChannelTrack track(core::TimeKeyType_Scale, 0);
    track.insertKey(2, 2.0f);
    track.insertKey(8, 8.0f);

    // The wrapped segment runs 8 -> 2 over the range's 11-frame period.
    check(near(track.sample(info(0, 0, 10, true)), 4.4f), "looped channel evaluates the wrapped segment");
    check(near(track.sample(info(0, 0, 10, false)), 2.0f), "loop off holds the first key before it");
    check(near(track.sample(info(10, 0, 10, true)), 5.6f), "looped channel wraps after the last key");
    check(near(track.sample(info(10, 0, 10, false)), 8.0f), "loop off holds the last key after it");

    std::printf("[5] loop: wrapped segment and hold fallback ok\n");
}

// ---------------------------------------------------------------------------
// 6. Clamping, quantization and ids.
// ---------------------------------------------------------------------------
void sectionClampAndIds() {
    core::ChannelTrack opa(core::TimeKeyType_Opacity, 0);
    opa.insertKey(0, 2.0f);
    check(near(opa.keyAt(0)->value(), 1.0f), "Opacity clamps a written value to 1");

    core::ChannelTrack scale(core::TimeKeyType_Scale, 0);
    scale.insertKey(0, 1.0e9f);
    check(near(scale.keyAt(0)->value(), 10000.0f), "Scale clamps a written value to its maximum");

    core::ChannelTrack hsv(core::TimeKeyType_HSV, 0);
    hsv.insertKey(0, 1.9f);
    check(near(hsv.keyAt(0)->value(), 1.0f), "HSV truncates to its integer storage");

    core::ChannelTrack ids(core::TimeKeyType_Scale, 0);
    const int firstId = ids.insertKey(0, 2.0f).id();
    const int secondId = ids.insertKey(10, 4.0f).id();
    check(firstId > 0 && secondId > 0 && firstId != secondId, "keys get positive, distinct ids");
    ids.insertKey(10, 7.0f);
    check(ids.keyAt(10)->id() == secondId, "replacing a key preserves its stable id");

    std::printf("[6] clamps, integer storage and stable ids ok\n");
}

// ---------------------------------------------------------------------------
// 7. Property and state tracks.
// ---------------------------------------------------------------------------
void sectionPropertyAndState() {
    core::PropertyTrack scale(core::TimeKeyType_Scale);
    check(scale.channelCount() == 2, "Scale exposes two channel tracks");
    check(scale.channel(0) != nullptr && scale.channel(1) != nullptr, "both Scale channels exist");
    check(scale.channel(2) == nullptr, "Scale has no third channel");
    check(scale.isEmpty(), "a fresh property is empty");

    scale.channel(0)->insertKey(0, 1.0f);
    scale.channel(0)->insertKey(10, 3.0f);
    scale.channel(1)->insertKey(0, 2.0f);
    scale.channel(1)->insertKey(20, 4.0f);
    check(!scale.isEmpty(), "a property with channel keys is not empty");
    check(near(scale.sampleChannel(0, info(5, 0, 20, false)), 2.0f), "property X samples its track");
    check(near(scale.sampleChannel(1, info(5, 0, 20, false)), 2.5f), "property Y samples its track");

    std::unique_ptr<core::PropertyTrack> copy = scale.clone();
    check(near(copy->sampleChannel(0, info(5, 0, 20, false)), 2.0f), "a property clone keeps values");
    check(copy->channel(0)->keyAt(0)->id() != scale.channel(0)->keyAt(0)->id(),
          "a property clone gives channel keys fresh ids");

    core::PropertyTrack move(core::TimeKeyType_Move);
    check(move.channelCount() == 4, "Move exposes position and centroid channels");
    check(move.channel(2) != nullptr && !move.channel(2)->spec().graphable,
          "Move's centroid channel is keyable but not graphable");
    move.channel(2)->insertKey(0, 5.0f);
    check(near(move.sampleChannel(2, info(0, 0, 10, false)), 5.0f), "a hidden channel still samples");

    auto baseState = std::make_unique<core::MoveStateKey>();
    baseState->setSpline(core::MoveKey::SplineType_Linear);
    move.state().setBaseState(std::move(baseState));
    auto catmull = std::make_unique<core::MoveStateKey>();
    catmull->setSpline(core::MoveKey::SplineType_CatmullRom);
    move.state().insertKey(10, std::move(catmull));
    auto linear = std::make_unique<core::MoveStateKey>();
    linear->setSpline(core::MoveKey::SplineType_Linear);
    move.state().insertKey(20, std::move(linear));

    const auto* at5 = static_cast<const core::MoveStateKey*>(move.state().sampleState(5));
    const auto* at15 = static_cast<const core::MoveStateKey*>(move.state().sampleState(15));
    const auto* at20 = static_cast<const core::MoveStateKey*>(move.state().sampleState(20));
    check(at5 && at5->spline() == core::MoveKey::SplineType_Linear, "state before the first key is the base");
    check(at15 && at15->spline() == core::MoveKey::SplineType_CatmullRom, "state is step-held");
    check(at20 && at20->spline() == core::MoveKey::SplineType_Linear, "a state key changes the state at its frame");

    check(move.state().moveKey(20, 30), "a state key moves into a free frame");
    const auto* at25 = static_cast<const core::MoveStateKey*>(move.state().sampleState(25));
    const auto* at30 = static_cast<const core::MoveStateKey*>(move.state().sampleState(30));
    check(at25 && at25->spline() == core::MoveKey::SplineType_CatmullRom, "the moved state leaves its old range");
    check(at30 && at30->spline() == core::MoveKey::SplineType_Linear, "the moved state is at its new frame");

    std::unique_ptr<core::StateTrack> stateCopy = move.state().clone();
    const auto* copyAt30 = static_cast<const core::MoveStateKey*>(stateCopy->sampleState(30));
    check(copyAt30 && copyAt30->spline() == core::MoveKey::SplineType_Linear, "a state track clone keeps its keys");

    std::printf("[7] property and state tracks: channels, hidden centroid, step-held state and clones\n");
}

// ---------------------------------------------------------------------------
// 8. Frame moves.
// ---------------------------------------------------------------------------
void sectionMoves() {
    core::ChannelTrack track(core::TimeKeyType_Scale, 0);
    track.insertKey(0, 1.0f);
    track.insertKey(10, 2.0f);

    check(track.moveKey(0, 5), "a key moves into a free frame");
    check(track.hasKey(5) && !track.hasKey(0), "the source frame is vacated");
    check(!track.moveKey(10, 5), "a move onto an occupied frame is refused");
    check(track.hasKey(10) && track.hasKey(5), "a refused move leaves both keys where they were");
    check(track.moveKey(10, 20), "a key moves past its old neighbour");
    check(track.hasKey(20) && !track.hasKey(10), "the second move vacates its source");

    std::printf("[8] frame moves: free frame accepted, occupied destination refused\n");
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main() {
    sectionSpecs();
    sectionInterpolation();
    sectionIndependentChannels();
    sectionStepHeld();
    sectionLoop();
    sectionClampAndIds();
    sectionPropertyAndState();
    sectionMoves();

    if (failures != 0) {
        std::printf("channel_track: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("channel_track: all checks passed\n");
    return 0;
}
