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

// THE SIZE/HARDNESS DRAG, decided in one place: the Alt drag over the viewport
// (horizontal = size, vertical = hardness). It lives here for the same reason the stroke does -
// every painting context shares one brush, so one gesture must mean one thing - and the widget
// only calls begin/move/end and forwards what it returns.
//
// NO LATCH, AND NEVER BOTH AXES IN ONE MOVE. The first version banked both axes on every move, so
// one vertical drag with 2px of sideways jitter changed the size too (measured: +7 size / -1
// hardness from a single drag). The second latched the DOMINANT axis for the whole gesture, which
// is the opposite mistake: a drag that starts horizontal and turns vertical could no longer change
// the hardness at all (reported). The rule now is per MOVE: the direction the pointer is moving IN
// THAT EVENT decides, and the other component is ignored - so a vertical stroke changes the
// hardness and never the size, a horizontal one the reverse, and a change of direction mid-gesture
// changes which value moves, immediately.
//
// The values are CONTINUOUS: the travel is handed over as a fractional number of steps, not rounded
// to whole notches, because rounding is what made the hardness snap in 5% jumps (a notch is 5% for
// the keys, and a drag should not be quantised by a keyboard's step size).
struct BrushDrag {
    // THE HOUSE SLOP, 4px (the marquee and the filter card's reorder use the same number): the
    // gesture is a DRAG on its own button (Alt+RIGHT), so a click that wobbles adjusts nothing.
    static constexpr float kAxisSlop = 4.0f;

    // One step per 8 logical px of travel for the size (a notch is x1.1) and per 40 for the
    // hardness (a notch is 5%); the DWELL of the gesture is the same as the keys', so a drag and a
    // key press move the value by the same amount per unit of intent.
    static constexpr float kSizePxPerStep = 8.0f;
    static constexpr float kHardnessPxPerStep = 40.0f;

    enum class Axis { None, Size, Hardness };

    // has the gesture moved at all? (an un-slid press is still a click: the Alt+left eyedropper
    // defers its sample until the release for exactly this)
    bool latched() const { return mMoved; }
    // where the gesture started (the gate and the caller's own bookkeeping read it)
    QPointF origin() const { return mOrigin; }

    void begin(const QPointF& aPos) {
        mOrigin = aPos;
        mCursor = aPos;
        mLast = aPos;
        mAxis = Axis::None;
        mMoved = false;
        mActive = true;
    }

    // The travel since the last call, as FRACTIONAL steps of each value (either may be 0). The
    // dominant direction of THIS move decides which one it is; the other stays 0. True when the
    // gesture is past its slop and the caller should apply them.
    bool move(const QPointF& aPos, float& aSizeSteps, float& aHardnessSteps) {
        aSizeSteps = 0.0f;
        aHardnessSteps = 0.0f;
        if (!mActive)
            return false;

        mCursor = aPos;
        if (!mMoved) {
            if (std::max(std::fabs(aPos.x() - mOrigin.x()), std::fabs(aPos.y() - mOrigin.y()))
                <= kAxisSlop) {
                mLast = aPos;
                return false;
            }
            mMoved = true;
            mLast = aPos; // the run-up is not applied: it is what the slop measured
            return false;
        }

        const float dx = float(aPos.x() - mLast.x());
        const float dy = float(aPos.y() - mLast.y());
        mLast = aPos;
        if (dx == 0.0f && dy == 0.0f)
            return false;

        // THE DIRECTION OF THIS MOVE decides. |dx| == |dy| counts as the size (the horizontal
        // gesture is the one users reach for first) and the ignored component is dropped, never
        // banked: banking it would make the size drift during a vertical drag.
        if (std::fabs(dx) >= std::fabs(dy)) {
            aSizeSteps = dx / kSizePxPerStep;
            mAxis = Axis::Size;
        } else {
            // dragging up raises the hardness: screen y grows downwards
            aHardnessSteps = -dy / kHardnessPxPerStep;
            mAxis = Axis::Hardness;
        }
        return true;
    }

    void end() {
        mActive = false;
        mMoved = false;
        mAxis = Axis::None;
    }

    bool isActive() const { return mActive; }
    Axis axis() const { return mAxis; }
    // where the pointer is now: the preview follows it, not the press point
    QPointF cursor() const { return mCursor; }

private:
    QPointF mOrigin; // the press point: what the slop is measured from
    QPointF mLast;   // the previous sample: the move's own delta comes from here
    QPointF mCursor; // the latest position
    Axis mAxis = Axis::None;
    bool mMoved = false;
    bool mActive = false;
};

} // namespace ctrl

#endif // CTRL_BRUSH_H
