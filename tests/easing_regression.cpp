// Regression test for the custom-bezier easing solver (src/util/Easing.cpp).
//
// The solver used to be Newton-only (guess t = x, up to 6 steps, break when
// |dx| < 1e-8, no bisection fallback, clamp t to [-2,2]). It was replaced by a
// port of WebKit's UnitBezier (WebCore/platform/graphics/UnitBezier.h):
// precomputed polynomial coefficients + an 11-point spline for the initial
// guess and bracket, up to 4 Newton steps that check the actual x-residual,
// then a bisection fallback. The old solver diverged on curves whose
// x-derivative approaches zero (e.g. control set (1,0,0,1): max error ~0.26).
//
// This test asserts the port matches an independent high-accuracy reference over
// the well-defined x in [0,1] domain, that Custom ignores weight while analytic
// easings still honor it, and that the header-exposed solver is correct and
// safe to call from the app's worker threads (it is a pure function of its
// control points). It exercises the solver directly (util::UnitBezier) as well
// as through util::Easing::calculate.
#include <QCoreApplication>
#include <QDebug>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include "util/Easing.h"
#include "util/UnitBezier.h"
#include "XCAssert.h"

namespace {
class AbortHandler : public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class NullErrorHandler : public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override {}
};
} // namespace

static AbortHandler gAbortHandler;
static NullErrorHandler gNullErrorHandler;
XCAssertHandler* gXCAssertHandler = &gAbortHandler;
XCErrorHandler* gXCErrorHandler = &gNullErrorHandler;

// Independent high-accuracy reference for the x->y mapping over x in [0,1].
// Deliberately not derived from the repo's UnitBezier, so a bad port fails.
static double refSolve(double x, double x1, double y1, double x2, double y2) {
    const double cx = 3.0 * x1;
    const double bx = 3.0 * (x2 - x1) - cx;
    const double ax = 1.0 - cx - bx;
    const double cy = 3.0 * y1;
    const double by = 3.0 * (y2 - y1) - cy;
    const double ay = 1.0 - cy - by;
    auto sx = [&](double t) { return ((ax * t + bx) * t + cx) * t; };
    auto sd = [&](double t) { return (3.0 * ax * t + 2.0 * bx) * t + cx; };
    auto sy = [&](double t) { return ((ay * t + by) * t + cy) * t; };
    // Bisection to bracket the root, then Newton to refine. Bisection keeps the
    // solve correct near zero x-derivative where Newton alone diverges.
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 100; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (sx(mid) < x)
            lo = mid;
        else
            hi = mid;
        if (hi - lo < 1e-16)
            break;
    }
    double t = 0.5 * (lo + hi);
    for (int i = 0; i < 50; ++i) {
        const double r = sx(t) - x;
        const double d = sd(t);
        if (std::abs(r) < 1e-14)
            break;
        if (std::abs(d) < 1e-14)
            break;
        t -= r / d;
        if (t < 0.0)
            t = 0.0;
        else if (t > 1.0)
            t = 1.0;
    }
    return sy(t);
}

// Evaluate the repo's custom-bezier easing at normalized progress t. weight
// pairs with the curve/linear output blend inside calculateBezier.
static double evalCurve(double t, double x1, double y1, double x2, double y2, float weight = 1.0f) {
    util::Easing::Param p;
    p.type = util::Easing::Type_Custom;
    p.range = util::Easing::Range_InOut;
    p.weight = weight;
    p.cubicBezier.x1 = float(x1);
    p.cubicBezier.y1 = float(y1);
    p.cubicBezier.x2 = float(x2);
    p.cubicBezier.y2 = float(y2);
    return double(util::Easing::calculate(p, float(t), 0.0f, 1.0f, 1.0f));
}

struct Curve {
    double x1, y1, x2, y2;
    const char* name;
};

static double tolFor(double ref) { return 1e-4 + 1e-4 * std::fabs(ref); }

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const Curve curves[] = {
        {0.0, 0.0, 1.0, 1.0, "linear"},
        {2.0 / 3, 0.0, 1.0 / 3, 1.0, "app-default ease-in-out"},
        {0.42, 0.0, 0.58, 1.0, "css ease-in-out"},
        {0.25, 0.1, 0.25, 1.0, "css ease"},
        {0.42, 0.0, 1.0, 1.0, "css ease-in"},
        {0.0, 0.0, 0.58, 1.0, "css ease-out"},
        {0.5, 0.0, 0.5, 1.0, "vertical (Newton-hard)"},
        {1.0, 0.0, 0.0, 1.0, "extreme cross"}, // old solver erred ~0.26 here
        {0.9, 0.1, 0.1, 0.9, "shallow s"},
    };
    const int numCurves = int(sizeof(curves) / sizeof(curves[0]));

    // 1) Solver correctness over the well-defined domain.
    for (const Curve& c : curves) {
        for (int k = 0; k <= 200; ++k) {
            const double t = double(k) / 200.0;
            const double ref = refSolve(t, c.x1, c.y1, c.x2, c.y2);
            const double got = evalCurve(t, c.x1, c.y1, c.x2, c.y2);
            if (std::fabs(got - ref) > tolFor(ref)) {
                XC_MSG_ASSERT(false, "curve %s t=%.3f got=%.6f ref=%.6f err=%.2e",
                              c.name, t, got, ref, std::fabs(got - ref));
            }
        }
    }

    // 2) Weight semantics: for a Custom bezier, weight is ignored -- a drawn
    //    curve is exactly that curve regardless of the Param's weight (the
    //    control is disabled in the UI as redundant). The analytic easing path
    //    still blends weight as curve<->linear.
    {
        const Curve& wc = curves[1]; // app-default ease-in-out
        for (int k = 0; k <= 100; ++k) {
            const double t = double(k) / 100.0;
            const double ref = refSolve(t, wc.x1, wc.y1, wc.x2, wc.y2);
            for (float w : {0.0f, 0.5f, 1.0f}) {
                const double v = evalCurve(t, wc.x1, wc.y1, wc.x2, wc.y2, w);
                XC_ASSERT(std::fabs(v - ref) <= tolFor(ref));
            }
        }
        // Analytic easings still honor weight (eased*w + linear*(1-w)).
        util::Easing::Param p;
        p.type = util::Easing::Type_Cubic;
        p.range = util::Easing::Range_InOut;
        for (int k = 0; k <= 100; ++k) {
            const double t = double(k) / 100.0;
            p.weight = 1.0f;
            const double curve = double(util::Easing::calculate(p, float(t), 0.0f, 1.0f, 1.0f));
            p.weight = 0.0f;
            const double lin = double(util::Easing::calculate(p, float(t), 0.0f, 1.0f, 1.0f));
            p.weight = 0.5f;
            const double mid = double(util::Easing::calculate(p, float(t), 0.0f, 1.0f, 1.0f));
            XC_ASSERT(std::fabs(lin - t) <= 1e-5);
            XC_ASSERT(std::fabs(mid - 0.5 * (curve + t)) <= 1e-5);
        }
    }

    // 3) Direct UnitBezier tests: the solver is now an independent,
    //    header-exposed type. Verify it head-to-head against the high-accuracy
    //    reference, plus a few closed-form landmarks.
    {
        for (const Curve& c : curves) {
            const util::UnitBezier ub(c.x1, c.y1, c.x2, c.y2);
            XC_ASSERT(std::fabs(ub.solve(0.0, util::UnitBezier::kBezierEpsilon) - 0.0) <= 1e-7);
            XC_ASSERT(std::fabs(ub.solve(1.0, util::UnitBezier::kBezierEpsilon) - 1.0) <= 1e-7);
            for (int k = 1; k < 100; ++k) {
                const double x = double(k) / 100.0;
                const double ref = refSolve(x, c.x1, c.y1, c.x2, c.y2);
                const double got = ub.solve(x, util::UnitBezier::kBezierEpsilon);
                XC_ASSERT(std::fabs(got - ref) <= tolFor(ref));
            }
        }
        // Control set (0,0,1,1): x(t)=y(t)=3t^2-2t^3, so the solve is the identity.
        const util::UnitBezier lin(0.0, 0.0, 1.0, 1.0);
        XC_ASSERT(std::fabs(lin.solve(0.3, util::UnitBezier::kBezierEpsilon) - 0.3) <= 1e-7);
        XC_ASSERT(std::fabs(lin.solve(0.7, util::UnitBezier::kBezierEpsilon) - 0.7) <= 1e-7);
        // Overshoot controls rise above y=1 inside the interval.
        const util::UnitBezier over(0.4, 1.5, 0.6, 1.5);
        XC_ASSERT(over.solve(0.5, util::UnitBezier::kBezierEpsilon) > 1.0);
    }

    // 4) The solver is a pure function of its control points (no shared mutable
    //    state), so it is safe to call from the app's worker threads. Stress it
    //    concurrently and require every result to match the reference.
    {
        const int kThreads = 4;
        const int kIters = 2000;
        std::atomic<int> failures{0};
        std::vector<std::thread> threads;
        for (int th = 0; th < kThreads; ++th) {
            threads.emplace_back([&, th]() {
                for (int i = 0; i < kIters; ++i) {
                    const Curve& c = curves[(th + i) % numCurves];
                    const double t = double(i % 100) / 100.0;
                    const double ref = refSolve(t, c.x1, c.y1, c.x2, c.y2);
                    const util::UnitBezier ub(c.x1, c.y1, c.x2, c.y2);
                    const double got = ub.solve(t, util::UnitBezier::kBezierEpsilon);
                    if (std::fabs(got - ref) > tolFor(ref))
                        ++failures;
                }
            });
        }
        for (auto& th : threads)
            th.join();
        XC_MSG_ASSERT(failures.load() == 0, "threaded stress failed %d times", int(failures.load()));
    }

    // 5) Param value-equality includes the bezier controls: two Params that
    //    differ only in cubicBezier are NOT equal (the change-detection stamp
    //    in EasingItem relies on this). Identical controls are equal.
    {
        util::Easing::Param a;
        a.type = util::Easing::Type_Custom;
        a.range = util::Easing::Range_InOut;
        a.weight = 1.0f;
        a.cubicBezier = util::Easing::CubicBezier{0.4f, 0.2f, 0.6f, 0.8f};

        util::Easing::Param same = a;
        util::Easing::Param diff = a;
        diff.cubicBezier.x1 = 0.45f;

        XC_ASSERT(same == a);
        XC_ASSERT(diff != a);
        XC_ASSERT(a.cubicBezier == same.cubicBezier);
        XC_ASSERT(!(a.cubicBezier == diff.cubicBezier));
    }

    std::printf("easing_regression: PASS (%d curves, solver + weight + threads ok)\n", numCurves);
    return 0;
}
