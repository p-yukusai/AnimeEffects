#ifndef UTIL_EASINGSEGMENT_H
#define UTIL_EASINGSEGMENT_H

#include <optional>

#include "util/Easing.h"
#include "util/UnitBezier.h"

namespace util {

// A PREPARED SEGMENT: everything a Custom curve needs that depends on the PAIR rather
// than on the sample.
//
// Why this exists, measured 2026-10-26 (400k evaluations, Debug, this machine):
//
//   controlPoint x2 alone (the derivation)      151.8 ns per sample
//   `Easing::calculate` on a derived pair       197.5 ns per sample
//   the same curve from a prepared segment        5.7 ns per sample
//   a graph polyline, 8 curves x 600 samples     948 us -> 27 us per repaint
//
// The per-sample cost is NOT the solve. It is the preparation: `ctrl::graph::Curve::
// sampleOver` walks the neighbouring keys, builds an `AutoContext`, derives two control
// points and only then evaluates - for EVERY point of a drawn curve, i.e. ~600 times per
// segment per repaint when the segment is prepared once.
//
// So the segment is the object that gets prepared once, and this type is where that
// preparation lives. `Easing::calculate` is a one-shot wrapper over it, which keeps ONE
// implementation: the thirds decision, the coefficients and the solver are stated here
// and nowhere else.
//
// NOT INSIDE `UnitBezier`. That header is a verbatim WebKit port and it is
// `tests/easing_regression.cpp`'s INDEPENDENT oracle: the thirds gate compares the shipped
// path against `UnitBezier::solve`, so a shortcut inside `solve` would make the gate
// compare the shipped path with itself.
//
// One-shot callers pay one construction (a `UnitBezier` is ~4 ns) and nothing else, so
// `Easing::calculate` keeps its signature and its behaviour; callers that sample a segment
// repeatedly (the graph polyline, the picker previews) hold one of these instead.
class EasingSegment {
public:
    // A STORED pair: the two control points come from the param's cubBezier as they are.
    EasingSegment(const Easing::Param& aParam, float aSpan);

    // A DERIVED pair: each side's control point comes from its handle mode (see
    // Easing::controlPoint), so the neighbours are part of what gets prepared.
    EasingSegment(const Easing::Param& aParam, const Easing::AutoContext& aCtx, float aSpan);

    // The eased RATE for a progress `aT` inside the segment, i.e. what
    // `Easing::calculate(param, aT, 0, 1, span)` returns. A step-held type returns the
    // same thing `calculate` does for it (the callers that hold steps handle them
    // themselves; this type is only ever built for a Custom param).
    float rate(float aT) const;

    // Whether this segment takes the thirds fast path (exposed for the gate: the branch a
    // build actually takes is otherwise invisible to a value comparison).
    bool usesThirdsPath() const { return mThirds; }

    // The prepared control points, narrowed to the floats the evaluation uses.
    float x1() const { return mX1; }
    float y1() const { return mY1; }
    float x2() const { return mX2; }
    float y2() const { return mY2; }

private:
    float evaluate(float aX) const;

    bool mThirds = false;
    float mSpan = 1.0f;
    float mX1 = 0.0f, mY1 = 0.0f, mX2 = 1.0f, mY2 = 1.0f;
    // Built ONLY when the pair is not on the thirds. `std::optional` rather than a member,
    // because a UnitBezier's constructor builds its 11-entry spline table: a default
    // member would pay that on the fast path too, which measured as the prepared segment
    // being SLOWER than the old one-shot code (250 ns against 37.5) until this changed.
    std::optional<UnitBezier> mBezier;
};

} // namespace util

#endif // UTIL_EASINGSEGMENT_H
