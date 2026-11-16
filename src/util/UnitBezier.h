#ifndef UTIL_UNITBEZIER
#define UTIL_UNITBEZIER

#include <algorithm>
#include <array>
#include <cmath>

namespace util {

// --------------------------------------------------------------------------
// UnitBezier — the cubic-bezier timing solver, ported verbatim from WebKit
// (WebCore/platform/graphics/UnitBezier.h, WebKit main). WebKit solves x(t)=x
// with a few Newton iterations bracketed by a precomputed spline of sample
// points, then falls back to bisection when Newton stalls near a zero
// x-derivative. The previous Newton-only solver (guess t=x, up to 6 steps,
// break on |dx|<1e-8, no fallback, clamp t to [-2,2]) diverged: max error ~0.26
// on the (1,0,0,1) curve vs ~4e-6 here. solve() extrapolates along the
// end-point gradients for x outside [0,1], as WebKit does for CSS timing
// functions.
//
// A UnitBezier is cheap to construct (11 Horner evals + coefficient setup), so
// callers may build one per evaluation without caching.
// --------------------------------------------------------------------------
class UnitBezier {
public:
    static constexpr double kBezierEpsilon = 1e-7;
    static constexpr int kMaxNewtonIterations = 4;
    static constexpr int kSplineSamples = 11;

    UnitBezier(const double p1x, const double p1y, const double p2x, const double p2y) {
        // Calculate the polynomial coefficients, implicit first and last
        // control points are (0,0) and (1,1).
        cx = 3.0 * p1x;
        bx = 3.0 * (p2x - p1x) - cx;
        ax = 1.0 - cx - bx;

        cy = 3.0 * p1y;
        by = 3.0 * (p2y - p1y) - cy;
        ay = 1.0 - cy - by;

        // End-point gradients are used to calculate timing function results
        // outside the range [0, 1].
        if (p1x > 0)
            startGradient = p1y / p1x;
        else if (!p1y && p2x > 0)
            startGradient = p2y / p2x;
        else if (!p1y && !p2y)
            startGradient = 1;
        else
            startGradient = 0;
        if (p2x < 1)
            endGradient = (p2y - 1) / (p2x - 1);
        else if (p2y == 1 && p1x < 1)
            endGradient = (p1y - 1) / (p1x - 1);
        else if (p2y == 1 && p1y == 1)
            endGradient = 1;
        else
            endGradient = 0;

        const double deltaT = 1.0 / (kSplineSamples - 1);
        for (int i = 0; i < kSplineSamples; i++)
            splineSamples[i] = sampleCurveX(i * deltaT);
    }

    double solve(const double x, const double epsilon) const {
        if (x < 0.0)
            return 0.0 + startGradient * x;
        if (x > 1.0)
            return 1.0 + endGradient * (x - 1.0);
        return sampleCurveY(solveCurveX(x, epsilon));
    }

private:
    double sampleCurveX(const double t) const { return ((ax * t + bx) * t + cx) * t; }
    double sampleCurveY(const double t) const { return ((ay * t + by) * t + cy) * t; }
    double sampleCurveDerivativeX(const double t) const { return (3.0 * ax * t + 2.0 * bx) * t + cx; }

    // Given an x value, find a parametric value it came from.
    double solveCurveX(const double x, const double epsilon) const {
        double t0 = 0.0;
        double t1 = 0.0;
        double t2 = x;
        double x2 = 0.0;
        double d2 = 0.0;
        int i = 0;

        // Linear interpolation of spline curve for initial guess.
        const double deltaT = 1.0 / (kSplineSamples - 1);
        for (i = 1; i < kSplineSamples; i++) {
            if (x <= splineSamples[i]) {
                t1 = deltaT * i;
                t0 = t1 - deltaT;
                t2 = t0 + (t1 - t0) * (x - splineSamples[i - 1]) / (splineSamples[i] - splineSamples[i - 1]);
                break;
            }
        }

        // Perform a few iterations of Newton's method -- normally very fast.
        const double newtonEpsilon = std::min(kBezierEpsilon, epsilon);
        for (i = 0; i < kMaxNewtonIterations; i++) {
            x2 = sampleCurveX(t2) - x;
            if (std::abs(x2) < newtonEpsilon)
                return t2;
            d2 = sampleCurveDerivativeX(t2);
            if (std::abs(d2) < kBezierEpsilon)
                break;
            t2 = t2 - x2 / d2;
        }
        if (std::abs(x2) < epsilon)
            return t2;

        // Fall back to the bisection method for reliability.
        // std::midpoint(t0, t1) is C++20; t0/t1 lie in [0,1] so the
        // overflow-safe expansion below is equivalent.
        while (t0 < t1) {
            x2 = sampleCurveX(t2);
            if (std::abs(x2 - x) < epsilon)
                return t2;
            if (x > x2)
                t0 = t2;
            else
                t1 = t2;
            t2 = t0 + (t1 - t0) * 0.5;
        }

        // Failure.
        return t2;
    }

    double ax;
    double bx;
    double cx;
    double ay;
    double by;
    double cy;
    double startGradient;
    double endGradient;
    std::array<double, kSplineSamples> splineSamples;
};

} // namespace util

#endif // UTIL_UNITBEZIER
