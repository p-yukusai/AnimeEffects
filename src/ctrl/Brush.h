#ifndef CTRL_BRUSH_H
#define CTRL_BRUSH_H

#include <QPointF>
#include <QVector2D>
#include <algorithm>
#include <cmath>
#include <functional>
#include "util/NonCopyable.h"

namespace ctrl {

// THE one brush model. Every painting context in the application shares it, so
// "size 40, hardness 60%" means the same thing whether it is painting a raster
// mask, warping FFD vertices, widening bone influence or erasing a pose - and a
// fix to the stroke (spacing, pressure ramps, the falloff) lands everywhere at
// once instead of in one tool and not the others.
//
// WHAT IS SHARED AND WHAT IS NOT. The stroke (where the stamps go), the falloff
// (what one stamp weighs at a distance) and the parameter names are shared. The
// EFFECT of a stamp is the target's: a raster target deposits alpha, the FFD
// target displaces vertices, an influence target adds weight. That is why the
// stamp carries an AMOUNT and a distance falloff and nothing else - a target that
// mixed those up would be the only thing that could disagree.
struct BrushParam {
    float radius = 100.0f;   // content (world) pixels
    float hardness = 0.5f;   // 0..1: the fraction of the radius at full strength
    float opacity = 1.0f;    // 0..1: the cap ONE stroke may reach (build-up is off)
    float flow = 1.0f;       // 0..1: what one stamp deposits
    bool pressureSize = false; // a tablet pen's pressure scales the radius
    bool pressureFlow = true;  // a tablet pen's pressure scales the amount
    float spacing = 0.125f;    // distance between stamps, as a fraction of the radius

    // the brush's erase mode: the same stamp, deposited the other way
    bool erase = false;

    // The step actually walked between stamps, in content pixels. Floored so a
    // hairline brush cannot ask for thousands of stamps per pixel of travel.
    float stampStep() const { return std::max(radius * spacing, 0.5f); }
};

// One stamp: the target applies brushFalloff() per element it covers and
// multiplies by `amount`; `erase` says which way to deposit it.
struct BrushStamp {
    QVector2D center;   // content (world) pixels
    QVector2D delta;    // travel since the previous stamp: what a WARPING target moves
                        // a point by, so a spaced stroke conserves its displacement
    float radius = 0.0f; // content pixels, pressure already applied
    float hardness = 0.0f;
    float amount = 0.0f; // flow * pressure, 0..1 (the falloff is NOT folded in)
    bool erase = false;
};

// The shared falloff, and the ONLY definition of what hardness means:
// Photoshop's model - a flat CORE of radius*hardness at full strength and a
// smoothstep band from there to the rim. hardness 1 leaves a hard edge (any
// antialiasing is the target's own filtering), hardness 0 a fully feathered disc.
// A distance at or past the rim weighs nothing.
inline float brushFalloff(float aDistance, float aRadius, float aHardness) {
    if (aRadius <= 0.0f || aDistance >= aRadius)
        return 0.0f;
    const float hardness = std::min(std::max(aHardness, 0.0f), 1.0f);
    const float core = aRadius * hardness;
    if (aDistance <= core)
        return 1.0f;
    // core < distance < radius, and core < radius here, so the divisor is positive
    const float t = (aRadius - aDistance) / (aRadius - core); // 1 at the core, 0 at the rim
    return t * t * (3.0f - 2.0f * t);
}

// Turns cursor samples into a spaced stream of stamps. Without this every tool
// stamped once per drag EVENT, so a fast stroke came out as dots - the defect the
// FFD brush has carried since it was written.
//
// begin() stamps once at the press point; move() walks the segment from the last
// stamp position in BrushParam::stampStep() steps, interpolating the pen pressure
// between the two samples so a pressure ramp is a ramp and not two steps; end()
// closes the stroke and emits nothing more.
class BrushStroke: private util::NonCopyable {
public:
    using Sink = std::function<void(const BrushStamp&)>;

    void begin(const BrushParam& aParam, const QVector2D& aPos, float aPressure, Sink aSink);
    void move(const QVector2D& aPos, float aPressure);
    void end();

    bool isActive() const { return mActive; }
    // how many stamps the current stroke has emitted (the gates read it)
    int stampCount() const { return mStampCount; }

private:
    // (`emit` is Qt's keyword macro, so the internal stamp emitter is named out)
    void stampAt(const QVector2D& aPos, float aPressure);

    BrushParam mParam;
    Sink mSink;
    QVector2D mLastPos;
    QVector2D mLastStampPos;
    float mLastPressure = 1.0f;
    // distance from mLastPos to the next stamp that is due
    float mNextStamp = 0.0f;
    bool mActive = false;
    int mStampCount = 0;
};

// THE SIZE/HARDNESS DRAG, decided in one place: the Alt + RIGHT gesture over the
// viewport (horizontal = size, vertical = hardness), as the notch keys do it one
// value at a time. It lives here for the same reason the stroke does - every
// painting context shares one brush, so one gesture must mean one thing - and the
// widget only calls begin/move/end and forwards the steps it returns.
//
// THIS LOCK IS AN AnimeEffects DECISION, NOT PHOTOSHOP PARITY. Photoshop's version
// of the gesture is a 2D PAD: horizontal drives the size and vertical the hardness
// at the same time, and the "Vary Round Brush Hardness based on HUD vertical
// movement" preference only turns the hardness half on or off. It has NO
// dominant-axis snap - there is nothing to copy. The snap is here because of the
// measured complaint: the old handler banked both axes unconditionally, so one
// vertical drag with 2px of sideways jitter banked size steps too (AGENTS.md
// records +7 size / -1 hardness from a single drag), and a value moving when the
// user did not aim at it is a defect, not a feature.
//
// THE RULES (the gate in tests/brush_stroke.cpp pins them):
// - nothing fires until the pointer has left kAxisSlop in EITHER direction, so a
//   click that wobbles adjusts nothing;
// - at that threshold the DOMINANT axis latches for the rest of the gesture and
//   the other bank is zeroed. The run-up travel is discarded, not banked: banking
//   it would fire the first step the moment the latch engaged, which is the exact
//   jump the slop exists to prevent;
// - afterwards only the latched axis' component is banked, in whole steps, and
//   the remainder is kept, so a slow drag still adds up.
//
// THE AXIS IS NOT RE-EVALUATED MID-DRAG. Around 45 degrees the |dx| > |dy| winner
// flips between samples, and flipping would have to drop the other axis' bank (it
// is one gesture) and make a value jump. A future "re-evaluate" is a different
// feel, not a fix: it must reset BOTH banks and re-arm the slop, i.e. begin a new
// gesture - a decision to make deliberately rather than a tweak.
struct BrushDrag {
    // 4px: the house slop (InspectorFilterCard begins its reorder drag past the
    // same number), and the smallest travel that reads as intent rather than hand
    // tremor.
    static constexpr float kAxisSlop = 4.0f;
    // One step per 8 logical px of the latched axis for the size, per 40 for the
    // hardness. These are the rates the event handler used to hold as literals,
    // moved here so the policy has one home instead of one per caller.
    static constexpr float kSizePxPerStep = 8.0f;
    static constexpr float kHardnessPxPerStep = 40.0f;

    enum class Axis { None, Size, Hardness };

    // Starts a gesture at the press point: no axis yet, no bank, nothing fired.
    void begin(const QPointF& aPos) {
        mOrigin = aPos;
        mLast = aPos;
        mAxis = Axis::None;
        mSizeBank = 0.0f;
        mHardnessBank = 0.0f;
        mActive = true;
    }

    // Banks the travel since the last call and reports the whole steps each value
    // earned (either may be 0); true when at least one fired. The latch itself
    // fires nothing (see the run-up rule above). A drag that has ended ignores
    // later moves, so a stray event cannot adjust a finished gesture.
    bool move(const QPointF& aPos, int& aSizeSteps, int& aHardnessSteps) {
        aSizeSteps = 0;
        aHardnessSteps = 0;
        if (!mActive)
            return false;

        if (mAxis == Axis::None) {
            const QPointF runUp = aPos - mOrigin;
            const float dx = float(runUp.x());
            const float dy = float(runUp.y());
            if (std::max(std::fabs(dx), std::fabs(dy)) <= kAxisSlop) {
                mLast = aPos;
                return false;
            }
            mAxis = std::fabs(dx) > std::fabs(dy) ? Axis::Size : Axis::Hardness;
            mSizeBank = 0.0f;
            mHardnessBank = 0.0f;
            mLast = aPos; // the run-up is discarded, not banked
            return false;
        }

        const QPointF delta = aPos - mLast;
        mLast = aPos;
        if (mAxis == Axis::Size) {
            mSizeBank += float(delta.x()) / kSizePxPerStep;
            aSizeSteps = int(mSizeBank);
            mSizeBank -= float(aSizeSteps);
        } else {
            // dragging up raises the hardness: screen y grows downwards, so the
            // vertical component is subtracted
            mHardnessBank -= float(delta.y()) / kHardnessPxPerStep;
            aHardnessSteps = int(mHardnessBank);
            mHardnessBank -= float(aHardnessSteps);
        }
        return aSizeSteps != 0 || aHardnessSteps != 0;
    }

    // Closes the gesture. The banks go with it: the next begin() starts clean.
    void end() {
        mActive = false;
        mAxis = Axis::None;
        mSizeBank = 0.0f;
        mHardnessBank = 0.0f;
    }

    bool isActive() const { return mActive; }
    Axis axis() const { return mAxis; }
    // the sub-step travel still owed to each value (the gate reads them)
    float sizeBank() const { return mSizeBank; }
    float hardnessBank() const { return mHardnessBank; }

private:
    QPointF mOrigin; // the press point: what the slop is measured from
    QPointF mLast;   // the previous sample: what the latched axis banks from
    Axis mAxis = Axis::None;
    float mSizeBank = 0.0f;
    float mHardnessBank = 0.0f;
    bool mActive = false;
};

} // namespace ctrl

#endif // CTRL_BRUSH_H
