// Contract tests for core::MoveEvaluator: the one place where Move's X/Y
// channels intentionally couple through its spatial Catmull-Rom path.
//
// What is pinned:
//   * paired X/Y key frames use the same spatial spline the old getMoveExpans
//     built, with X read at X's eased rate and Y at Y's;
//   * when the key frames differ the channels sample independently;
//   * centroid is a pair of ordinary hidden channels;
//   * the spline mode is step-held state, and a linear state falls back to the
//     scalar answer (the old linear path is the same straight segment).
//
// Headless: no QApplication, no widgets, no GL.
#include <QVector2D>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "XCAssert.h"
#include "core/ChannelTrack.h"
#include "core/MoveEvaluator.h"
#include "core/MoveKey.h"
#include "core/MoveStateKey.h"
#include "core/PropertyTrack.h"
#include "core/TimeInfo.h"
#include "util/Easing.h"
#include "util/FergusonCoonsSpline.h"

namespace {

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

bool near(float aLhs, float aRhs, float aEpsilon = 1e-4f) {
    return std::fabs(aLhs - aRhs) <= aEpsilon;
}

core::TimeInfo info(int aFrame, int aMin, int aMax, bool aLoop) {
    core::TimeInfo time;
    time.frame = core::Frame(aFrame);
    time.frameRange = util::Range(aMin, aMax);
    time.loop = aLoop;
    return time;
}

util::Easing::Param eased(util::Easing::Type aType) {
    util::Easing::Param param;
    param.type = aType;
    param.range = util::Easing::Range_InOut;
    return param;
}

void setSpline(core::PropertyTrack& aProperty, int aFrame, core::MoveKey::SplineType aSpline) {
    auto key = std::make_unique<core::MoveStateKey>();
    key->setSpline(aSpline);
    if (aFrame == core::TimeKey::kBaseValueIndex) {
        aProperty.state().setBaseState(std::move(key));
    } else {
        aProperty.state().insertKey(aFrame, std::move(key));
    }
}

// The pre-track formula, written independently of MoveEvaluator: a Ferguson-
// Coons spline through the paired positions, x read at tx and y at ty.
QVector2D legacyPairedSample(
    const QVector2D* p0, const QVector2D& p1, const QVector2D& p2, const QVector2D* p3,
    bool fromLinear, bool toLinear, float tx, float ty
) {
    const auto vels = core::MoveKey::getCatmullRomVels(p1, p2, p0, p3, fromLinear, toLinear);
    util::FergusonCoonsSpline<QVector3D> spline;
    spline.set(p1.toVector3D(), p2.toVector3D(), vels[0].toVector3D(), vels[1].toVector3D());
    return QVector2D(spline.getByLinear(tx).x(), spline.getByLinear(ty).y());
}

void sectionPairedSpline() {
    core::PropertyTrack move(core::TimeKeyType_Move);
    setSpline(move, core::TimeKey::kBaseValueIndex, core::MoveKey::SplineType_CatmullRom);

    move.channel(0)->insertKey(0, 0.0f, eased(util::Easing::Type_Linear));
    move.channel(0)->insertKey(10, 10.0f, eased(util::Easing::Type_Quad));
    move.channel(0)->insertKey(20, 10.0f, eased(util::Easing::Type_Linear));
    move.channel(1)->insertKey(0, 0.0f, eased(util::Easing::Type_Linear));
    move.channel(1)->insertKey(10, 5.0f, eased(util::Easing::Type_Linear));
    move.channel(1)->insertKey(20, 10.0f, eased(util::Easing::Type_Linear));

    check(core::MoveEvaluator::hasPairedChannels(move), "same X/Y key frames are paired");

    const core::TimeInfo at5 = info(5, 0, 20, false);
    const auto sx = move.channel(0)->sampleSegment(at5);
    const auto sy = move.channel(1)->sampleSegment(at5);
    check(sx.hasSegment() && sy.hasSegment(), "frame 5 lies inside both paired segments");

    const QVector2D p3(10.0f, 10.0f);
    const QVector2D expected = legacyPairedSample(
        nullptr, QVector2D(0.0f, 0.0f), QVector2D(10.0f, 5.0f),
        &p3, false, false, sx.rate, sy.rate
    );
    const QVector2D actual = core::MoveEvaluator::samplePosition(move, at5);
    check(near(actual.x(), expected.x()), "paired X follows the spatial spline at X's rate");
    check(near(actual.y(), expected.y()), "paired Y follows the spatial spline at Y's rate");

    std::printf("[1] paired Move: spatial spline keeps the old per-axis rates\n");
}

void sectionSeparateChannels() {
    core::PropertyTrack move(core::TimeKeyType_Move);
    setSpline(move, core::TimeKey::kBaseValueIndex, core::MoveKey::SplineType_CatmullRom);

    move.channel(0)->insertKey(0, 0.0f);
    move.channel(0)->insertKey(10, 10.0f);
    move.channel(1)->insertKey(0, 0.0f);
    move.channel(1)->insertKey(20, 20.0f);

    check(!core::MoveEvaluator::hasPairedChannels(move), "different X/Y key frames are not paired");
    const QVector2D at10 = core::MoveEvaluator::samplePosition(move, info(10, 0, 20, false));
    check(near(at10.x(), 10.0f), "X is exact at its key");
    check(near(at10.y(), 10.0f), "Y interpolates independently at the same frame");

    const QVector2D at15 = core::MoveEvaluator::samplePosition(move, info(15, 0, 20, false));
    check(near(at15.x(), 10.0f), "X holds after its last key");
    check(near(at15.y(), 15.0f), "Y continues on its own curve");

    std::printf("[2] separate Move: X and Y sample independently\n");
}

void sectionCentroid() {
    core::PropertyTrack move(core::TimeKeyType_Move);
    move.channel(2)->insertKey(0, 0.0f);
    move.channel(2)->insertKey(10, 10.0f);
    move.channel(3)->insertKey(0, 4.0f);

    const QVector2D centroid = core::MoveEvaluator::sampleCentroid(move, info(5, 0, 10, false));
    check(near(centroid.x(), 5.0f), "centroid X is an ordinary hidden channel");
    check(near(centroid.y(), 4.0f), "centroid Y holds independently");

    std::printf("[3] centroid: hidden channels sample like values\n");
}

void sectionLinearState() {
    core::PropertyTrack move(core::TimeKeyType_Move);
    setSpline(move, 0, core::MoveKey::SplineType_CatmullRom);
    setSpline(move, 10, core::MoveKey::SplineType_Linear);
    move.channel(0)->insertKey(0, 0.0f);
    move.channel(0)->insertKey(10, 10.0f);
    move.channel(1)->insertKey(0, 0.0f);
    move.channel(1)->insertKey(10, 10.0f);

    // Two keys with a linear arrival: the old velocity rule uses the chord, so
    // the curve is the straight line and the scalar answer is exact.
    const QVector2D at5 = core::MoveEvaluator::samplePosition(move, info(5, 0, 10, false));
    check(near(at5.x(), 5.0f) && near(at5.y(), 5.0f), "a linear spline state matches the scalar path");

    std::printf("[4] spline state: step-held, linear falls back to the scalar answer\n");
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main() {
    sectionPairedSpline();
    sectionSeparateChannels();
    sectionCentroid();
    sectionLinearState();

    if (failures != 0) {
        std::printf("move_evaluator: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("move_evaluator: all checks passed\n");
    return 0;
}
