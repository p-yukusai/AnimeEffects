#include "util/EasingSegment.h"

namespace util {

namespace {

// The identity this whole path rests on. For P1 = (1/3, y1) and P2 = (2/3, y2) the
// bezier's x-component is
//
//     x(t) = 3(1-t)^2 t/3 + 3(1-t) t^2 2/3 + t^3 = t,
//
// so the x->t solve is the identity and the curve is the plain cubic
//
//     y(x) = 3(1-x)^2 x y1 + 3(1-x) x^2 y2 + x^3,
//
// which is UnitBezier's own Horner form (cy = 3 y1, by = 3(y2 - y1) - cy, ay = 1 - cy - by)
// evaluated once at x. The coefficients stay in double and the result is narrowed at the
// end, exactly as the solved path does, so the two agree to within their own rounding
// rather than one carrying a longer tail.
//
// The thirds are FLOATS, so the identity is not literal: fl(1/3) is 9.93e-9 above 1/3
// (fl(2/3) is exactly twice that), which leaves x(t) - t = 3t(1-t^2)*9.93e-9 <= 1.15e-8.
// That is an order of magnitude below UnitBezier::kBezierEpsilon (1e-7), so this returns
// the numbers the solve returns; what it saves is the machinery around them.
//
// THE TEST IS EXACT FLOAT EQUALITY, never an epsilon. Every intended producer writes those
// exact floats (controlPoint answers 1.0/3.0 and 2.0/3.0 verbatim for a derived side, and a
// frozen ghost keeps them), so an epsilon would buy no extra hits while admitting pairs
// where y(x) = B_y(x) is NOT the curve. An epsilon remains defensible for a future caller
// that stores x approximately; if one is ever wanted, bound it by UnitBezier's own
// kBezierEpsilon so the fast path can never disagree with the solver by more than the
// solver's own tolerance.
bool isThirdsPair(const float aX1, const float aX2) {
    return aX1 == 1.0f / 3.0f && aX2 == 2.0f / 3.0f;
}

// y(x) for such a pair - the cubic in x, with no t to solve for.
float thirdsPairY(const float aX, const float aY1, const float aY2) {
    const double x = static_cast<double>(aX);
    const double y1 = static_cast<double>(aY1);
    const double y2 = static_cast<double>(aY2);
    const double cy = 3.0 * y1;
    const double by = 3.0 * (y2 - y1) - cy;
    const double ay = 1.0 - cy - by;
    return static_cast<float>(((ay * x + by) * x + cy) * x);
}

} // namespace

EasingSegment::EasingSegment(const Easing::Param& aParam, const float aSpan):
    mThirds(false), mSpan(aSpan == 0.0f ? 1.0f : aSpan) {
    const auto [x1, y1, x2, y2] = aParam.cubicBezier;
    mX1 = x1;
    mY1 = y1;
    mX2 = x2;
    mY2 = y2;
    mThirds = isThirdsPair(mX1, mX2);
    if (!mThirds) mBezier.emplace(mX1, mY1, mX2, mY2);
}

EasingSegment::EasingSegment(const Easing::Param& aParam, const Easing::AutoContext& aCtx,
                             const float aSpan):
    mThirds(false), mSpan(aSpan == 0.0f ? 1.0f : aSpan) {
    // The derivation works in double; the curve has always been float, and the fast path
    // must test the same floats the solve would receive - not the doubles they came from.
    const QPointF first = Easing::controlPoint(aParam, aCtx.inMode, aCtx, mSpan, 0);
    const QPointF second = Easing::controlPoint(aParam, aCtx.inMode, aCtx, mSpan, 1);
    mX1 = static_cast<float>(first.x());
    mY1 = static_cast<float>(first.y());
    mX2 = static_cast<float>(second.x());
    mY2 = static_cast<float>(second.y());
    mThirds = isThirdsPair(mX1, mX2);
    if (!mThirds) mBezier.emplace(mX1, mY1, mX2, mY2);
}

float EasingSegment::evaluate(const float aX) const {
    // The [0,1] gate is not optional: UnitBezier::solve extrapolates along its endpoint
    // GRADIENTS outside the interval, while the extended cubic is a different curve out
    // there, so the solve must keep that regime.
    if (mThirds && 0.0f <= aX && aX <= 1.0f)
        return thirdsPairY(aX, mY1, mY2);
    return static_cast<float>(mBezier->solve(aX, UnitBezier::kBezierEpsilon));
}

float EasingSegment::rate(const float aT) const {
    // Normalized progress. This is the x fed to the curve; it must NOT depend on weight,
    // and a custom bezier IS the drawn curve - its handles already span every shape weight
    // could reach. Keeping weight out of the maths means a stale weight on a Custom key (a
    // value left over from another easing type, or one read from a saved file) can never
    // silently skew a curve.
    return evaluate(aT / mSpan);
}

} // namespace util
